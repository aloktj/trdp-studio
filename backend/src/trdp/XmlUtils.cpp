#include "trdp/XmlUtils.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_map>

namespace trdp::xml {

namespace {

std::unordered_map<std::string, std::string> parseAttributes(const std::string &raw) {
    std::unordered_map<std::string, std::string> result;
    static const std::regex attr_regex{R"(([A-Za-z0-9_:\-.]+)\s*=\s*\"([^\"]*)\")"};
    auto begin = std::sregex_iterator(raw.begin(), raw.end(), attr_regex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        result[it->str(1)] = it->str(2);
    }
    return result;
}

}  // namespace

std::string trimCopy(const std::string &value) {
    auto begin = value.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return "";
    }
    auto end = value.find_last_not_of(" \t\n\r");
    return value.substr(begin, end - begin + 1);
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int safeStoi(const std::string &value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::vector<uint8_t> parseHexPayload(const std::string &raw) {
    std::string filtered;
    filtered.reserve(raw.size());
    for (char ch : raw) {
        if (std::isxdigit(static_cast<unsigned char>(ch))) {
            filtered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    if (filtered.empty()) {
        return {};
    }
    if (filtered.size() % 2 != 0U) {
        filtered.insert(filtered.begin(), '0');
    }
    std::vector<uint8_t> payload;
    payload.reserve(filtered.size() / 2);
    for (std::size_t i = 0; i < filtered.size(); i += 2) {
        auto byte_str = filtered.substr(i, 2);
        uint8_t value = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        payload.push_back(value);
    }
    return payload;
}

std::vector<XmlElement> extractElements(const std::string &xml, const std::string &tag) {
    std::vector<XmlElement> elements;
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";
    std::size_t pos = 0;
    while ((pos = xml.find(open, pos)) != std::string::npos) {
        const std::size_t head_start = pos + open.size();
        if (head_start < xml.size()) {
            const char boundary = xml[head_start];
            if (!(std::isspace(static_cast<unsigned char>(boundary)) || boundary == '>' || boundary == '/')) {
                pos = head_start;
                continue;
            }
        }
        std::size_t closing = xml.find('>', head_start);
        if (closing == std::string::npos) {
            break;
        }
        bool self_closing = closing > head_start && xml[closing - 1] == '/';
        std::string attr_segment = xml.substr(head_start, closing - head_start);
        if (self_closing && !attr_segment.empty()) {
            attr_segment.pop_back();
        }
        XmlElement element;
        element.attributes = parseAttributes(attr_segment);
        if (!self_closing) {
            auto close_pos = xml.find(close, closing + 1);
            if (close_pos == std::string::npos) {
                break;
            }
            element.body = xml.substr(closing + 1, close_pos - (closing + 1));
            pos = close_pos + close.size();
        } else {
            pos = closing + 1;
        }
        elements.push_back(std::move(element));
    }
    return elements;
}

}  // namespace trdp::xml
