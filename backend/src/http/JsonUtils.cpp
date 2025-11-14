#include "http/JsonUtils.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "auth/User.hpp"
#include "trdp/TrdpEngine.hpp"
#include "util/LogService.hpp"

namespace trdp::http::json {

namespace {

std::string trimCopy(const std::string &value) {
    const auto begin = value.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\n\r");
    return value.substr(begin, end - begin + 1);
}

std::string bytesToHexSpan(const uint8_t *data, size_t size) {
    if (data == nullptr || size == 0) {
        return "";
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        const uint8_t byte = data[i];
        hex.push_back(kHex[(byte >> 4) & 0x0F]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    return hex;
}

}  // namespace

std::string escape(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string error(const std::string &message) {
    return std::string{"{\"error\":\""} + escape(message) + "\"}";
}

std::optional<std::string> stringField(const std::string &body, const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    auto colon_pos = body.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    auto start_quote = body.find('"', colon_pos);
    if (start_quote == std::string::npos) {
        return std::nullopt;
    }
    auto end_quote = body.find('"', start_quote + 1);
    if (end_quote == std::string::npos) {
        return std::nullopt;
    }
    return body.substr(start_quote + 1, end_quote - start_quote - 1);
}

std::optional<int> intField(const std::string &body, const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    auto colon_pos = body.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    auto value_start = body.find_first_of("-0123456789", colon_pos + 1);
    if (value_start == std::string::npos) {
        return std::nullopt;
    }
    auto value_end = value_start;
    while (value_end < body.size() && std::isdigit(static_cast<unsigned char>(body[value_end]))) {
        ++value_end;
    }
    try {
        return std::stoi(body.substr(value_start, value_end - value_start));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::string>> stringArrayField(const std::string &body,
                                                         const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    auto open_bracket = body.find('[', key_pos + needle.size());
    if (open_bracket == std::string::npos) {
        return std::nullopt;
    }
    auto close_bracket = body.find(']', open_bracket + 1);
    if (close_bracket == std::string::npos) {
        return std::nullopt;
    }
    std::vector<std::string> values;
    size_t cursor = open_bracket + 1;
    while (cursor < close_bracket) {
        auto quote_start = body.find('"', cursor);
        if (quote_start == std::string::npos || quote_start >= close_bracket) {
            break;
        }
        auto quote_end = body.find('"', quote_start + 1);
        if (quote_end == std::string::npos || quote_end > close_bracket) {
            return std::nullopt;
        }
        values.push_back(body.substr(quote_start + 1, quote_end - quote_start - 1));
        cursor = quote_end + 1;
    }
    return values;
}

std::optional<std::vector<uint8_t>> parseHex(const std::string &hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    auto hexValue = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return 10 + (ch - 'a');
        }
        if (ch >= 'A' && ch <= 'F') {
            return 10 + (ch - 'A');
        }
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexValue(hex[i]);
        int lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return bytes;
}

std::optional<std::vector<uint8_t>> hexToBlob(const std::string &hex) {
    return parseHex(hex);
}

std::string bytesToHex(const std::vector<uint8_t> &data) {
    return bytesToHexSpan(data.data(), data.size());
}

std::string blobToHex(const void *data, size_t size) {
    return bytesToHexSpan(static_cast<const uint8_t *>(data), size);
}

std::string payloadAscii(const std::vector<uint8_t> &data) {
    std::string ascii;
    ascii.reserve(data.size());
    for (uint8_t byte : data) {
        if (byte >= 32 && byte <= 126) {
            ascii.push_back(static_cast<char>(byte));
        } else {
            ascii.push_back('.');
        }
    }
    return ascii;
}

std::string endpointIp(const std::string &endpoint) {
    auto cleaned = trimCopy(endpoint);
    auto pos = cleaned.find(':');
    if (pos == std::string::npos) {
        return cleaned;
    }
    return cleaned.substr(0, pos);
}

std::string pdListJson(const std::vector<stack::PdMessage> &messages, bool include_cycle_time) {
    std::string payload = "[";
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i != 0) {
            payload += ",";
        }
        payload += "{\"id\":" + std::to_string(messages[i].id) + ",";
        payload += "\"name\":\"" + escape(messages[i].name) + "\",";
        if (include_cycle_time) {
            payload += "\"cycle_time_ms\":" + std::to_string(messages[i].cycle_time_ms) + ",";
        }
        payload += "\"payload_hex\":\"" + bytesToHex(messages[i].payload) + "\",";
        payload += "\"last_update_utc\":\"" + escape(messages[i].timestamp) + "\"}";
    }
    payload += "]";
    return payload;
}

std::string pdDetailJson(const stack::PdMessage &message) {
    std::string payload = "{";
    payload += "\"id\":" + std::to_string(message.id) + ",";
    payload += "\"name\":\"" + escape(message.name) + "\",";
    payload += "\"cycle_time_ms\":" + std::to_string(message.cycle_time_ms) + ",";
    payload += "\"payload_hex\":\"" + bytesToHex(message.payload) + "\",";
    payload += "\"payload_ascii\":\"" + escape(payloadAscii(message.payload)) + "\",";
    payload += "\"last_update_utc\":\"" + escape(message.timestamp) + "\"}";
    return payload;
}

std::string mdIncomingListJson(const std::vector<stack::MdMessage> &messages) {
    std::string payload = "[";
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i != 0) {
            payload += ",";
        }
        payload += "{\"id\":" + std::to_string(messages[i].id) + ",";
        payload += "\"source_ip\":\"" + escape(endpointIp(messages[i].source)) + "\",";
        payload += "\"msg_id\":" + std::to_string(messages[i].msg_id) + ",";
        payload += "\"payload_hex\":\"" + bytesToHex(messages[i].payload) + "\",";
        payload += "\"timestamp_utc\":\"" + escape(messages[i].timestamp) + "\"}";
    }
    payload += "]";
    return payload;
}

std::string mdSendResponseJson(const stack::MdMessage &message) {
    std::string payload = "{";
    payload += "\"request_id\":" + std::to_string(message.id) + ",";
    payload += "\"msg_id\":" + std::to_string(message.msg_id) + ",";
    payload += "\"destination_ip\":\"" + escape(endpointIp(message.destination)) + "\",";
    payload += "\"payload_hex\":\"" + bytesToHex(message.payload) + "\",";
    payload += "\"timestamp_utc\":\"" + escape(message.timestamp) + "\",";
    payload += "\"status\":\"sent\"}";
    return payload;
}

std::string trdpLogListJson(const std::vector<util::TrdpLogEntry> &logs) {
    std::string payload = "[";
    for (size_t i = 0; i < logs.size(); ++i) {
        if (i != 0) {
            payload += ",";
        }
        payload += "{\"id\":" + std::to_string(logs[i].id) + ",";
        payload += "\"direction\":\"" + escape(logs[i].direction) + "\",";
        payload += "\"type\":\"" + escape(logs[i].type) + "\",";
        payload += "\"msg_id\":" + std::to_string(logs[i].msg_id) + ",";
        payload += "\"src_ip\":\"" + escape(logs[i].src_ip) + "\",";
        payload += "\"dst_ip\":\"" + escape(logs[i].dst_ip) + "\",";
        payload += "\"payload_hex\":\"" + blobToHex(logs[i].payload.data(), logs[i].payload.size()) + "\",";
        payload += "\"timestamp_utc\":\"" + escape(logs[i].timestamp) + "\"}";
    }
    payload += "]";
    return payload;
}

std::string appLogListJson(const std::vector<util::AppLogEntry> &logs) {
    std::string payload = "[";
    for (size_t i = 0; i < logs.size(); ++i) {
        if (i != 0) {
            payload += ",";
        }
        payload += "{\"id\":" + std::to_string(logs[i].id) + ",";
        payload += "\"level\":\"" + escape(logs[i].level) + "\",";
        payload += "\"message\":\"" + escape(logs[i].message) + "\",";
        payload += "\"timestamp_utc\":\"" + escape(logs[i].timestamp) + "\"}";
    }
    payload += "]";
    return payload;
}

std::string userJson(const auth::User &user) {
    return std::string{"{"} + "\"id\":" + std::to_string(user.id) + "," +
           "\"username\":\"" + escape(user.username) + "\"," +
           "\"role\":\"" + escape(user.role) + "\"," +
           "\"created_at\":\"" + escape(user.created_at) + "\"}";
}

std::string userListJson(const std::vector<auth::User> &users) {
    std::string payload = "[";
    for (size_t i = 0; i < users.size(); ++i) {
        if (i != 0) {
            payload += ",";
        }
        payload += userJson(users[i]);
    }
    payload += "]";
    return payload;
}

}  // namespace trdp::http::json

