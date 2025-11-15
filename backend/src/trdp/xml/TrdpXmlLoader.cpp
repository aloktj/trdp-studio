#include "trdp/xml/TrdpXmlLoader.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_map>

namespace {

struct XmlElement {
    std::unordered_map<std::string, std::string> attributes;
    std::string body;
};

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

int safeStoi(const std::string &value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::unordered_map<std::string, std::string> parseAttributes(const std::string &raw) {
    std::unordered_map<std::string, std::string> result;
    static const std::regex attr_regex{R"(([A-Za-z0-9_:\-\.]+)\s*=\s*\"([^\"]*)\")"};
    auto begin = std::sregex_iterator(raw.begin(), raw.end(), attr_regex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        result[it->str(1)] = it->str(2);
    }
    return result;
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

std::string extractAttribute(const std::unordered_map<std::string, std::string> &attrs, const std::string &key) {
    auto it = attrs.find(key);
    if (it != attrs.end()) {
        return trimCopy(it->second);
    }
    return "";
}

std::string extractEndpointFromBody(const std::string &body, const std::string &tag) {
    auto elements = extractElements(body, tag);
    for (const auto &element : elements) {
        if (auto attr_it = element.attributes.find("endpoint"); attr_it != element.attributes.end()) {
            return trimCopy(attr_it->second);
        }
        if (!element.body.empty()) {
            return trimCopy(element.body);
        }
    }
    return "";
}

}  // namespace

namespace trdp::xml {

bool ParsedTrdpConfig::hasStructuredTelegrams() const {
    for (const auto &iface : interfaces) {
        if (!iface.telegrams.empty()) {
            return true;
        }
    }
    return false;
}

namespace {

TelegramDirection parseDirection(const std::unordered_map<std::string, std::string> &attributes,
                                 TelegramKind kind) {
    auto dir = toLowerCopy(extractAttribute(attributes, "direction"));
    if (dir.empty() && kind == TelegramKind::kMd) {
        dir = "publisher";
    }
    if (dir == "subscriber" || dir == "listener" || dir == "sink" || dir == "in" || dir == "receive" ||
        dir == "source") {
        return TelegramDirection::kSubscriber;
    }
    return TelegramDirection::kPublisher;
}

ParsedTelegram parseTelegramElement(const XmlElement &element) {
    ParsedTelegram telegram;
    auto type_attr = toLowerCopy(extractAttribute(element.attributes, "type"));
    if (type_attr == "md" || type_attr == "message" || type_attr == "management") {
        telegram.kind = TelegramKind::kMd;
    }
    telegram.direction = parseDirection(element.attributes, telegram.kind);
    telegram.name = extractAttribute(element.attributes, "name");
    telegram.com_id = safeStoi(extractAttribute(element.attributes, "com-id"));
    if (telegram.com_id <= 0) {
        telegram.com_id = safeStoi(extractAttribute(element.attributes, "comId"));
    }
    telegram.dataset_id = safeStoi(extractAttribute(element.attributes, "dataset-id"));
    if (telegram.dataset_id <= 0) {
        telegram.dataset_id = safeStoi(extractAttribute(element.attributes, "datasetId"));
    }
    telegram.cycle_time_ms = safeStoi(extractAttribute(element.attributes, "cycle"));
    if (telegram.cycle_time_ms <= 0) {
        telegram.cycle_time_ms = safeStoi(extractAttribute(element.attributes, "interval"));
    }
    std::string payload_str = extractAttribute(element.attributes, "payload");
    if (payload_str.empty()) {
        auto payload_elements = extractElements(element.body, "payload");
        if (!payload_elements.empty()) {
            payload_str = payload_elements.front().body;
        }
    }
    telegram.default_payload = parseHexPayload(payload_str);
    auto src = extractAttribute(element.attributes, "source");
    if (src.empty()) {
        src = extractEndpointFromBody(element.body, "source");
    }
    telegram.source.endpoint = src;
    auto dst = extractAttribute(element.attributes, "destination");
    if (dst.empty()) {
        dst = extractEndpointFromBody(element.body, "destination");
    }
    telegram.destination.endpoint = dst;
    return telegram;
}

std::vector<ParsedTelegram> parseInterfaceTelegrams(const XmlElement &iface_element) {
    std::vector<ParsedTelegram> telegrams;
    auto telegram_elements = extractElements(iface_element.body, "telegram");
    for (const auto &element : telegram_elements) {
        telegrams.push_back(parseTelegramElement(element));
    }
    if (telegrams.empty()) {
        auto pd_elements = extractElements(iface_element.body, "pd");
        for (const auto &element : pd_elements) {
            auto telegram = parseTelegramElement(element);
            telegram.kind = TelegramKind::kPd;
            telegrams.push_back(telegram);
        }
        auto md_elements = extractElements(iface_element.body, "md");
        for (const auto &element : md_elements) {
            auto telegram = parseTelegramElement(element);
            telegram.kind = TelegramKind::kMd;
            telegrams.push_back(telegram);
        }
    }
    return telegrams;
}

ParsedInterfaceConfig parseInterfaceElement(const XmlElement &element) {
    ParsedInterfaceConfig iface;
    iface.name = extractAttribute(element.attributes, "name");
    iface.telegrams = parseInterfaceTelegrams(element);
    return iface;
}

ParsedTelegram parseLegacyPd(const XmlElement &element) {
    ParsedTelegram telegram;
    telegram.kind = TelegramKind::kPd;
    telegram.direction = TelegramDirection::kPublisher;
    auto dir = toLowerCopy(extractAttribute(element.attributes, "direction"));
    if (dir == "in" || dir == "incoming" || dir == "subscriber") {
        telegram.direction = TelegramDirection::kSubscriber;
    }
    telegram.name = extractAttribute(element.attributes, "name");
    telegram.com_id = safeStoi(extractAttribute(element.attributes, "id"));
    telegram.cycle_time_ms = safeStoi(extractAttribute(element.attributes, "cycle"));
    telegram.source.endpoint = extractAttribute(element.attributes, "source");
    telegram.destination.endpoint = extractAttribute(element.attributes, "destination");
    std::string payload;
    if (auto attr = element.attributes.find("payload"); attr != element.attributes.end()) {
        payload = attr->second;
    } else {
        payload = element.body;
    }
    telegram.default_payload = parseHexPayload(payload);
    return telegram;
}

ParsedTelegram parseLegacyMd(const XmlElement &element) {
    ParsedTelegram telegram;
    telegram.kind = TelegramKind::kMd;
    telegram.direction = TelegramDirection::kPublisher;
    telegram.name = extractAttribute(element.attributes, "name");
    telegram.com_id = safeStoi(extractAttribute(element.attributes, "id"));
    telegram.source.endpoint = extractAttribute(element.attributes, "source");
    telegram.destination.endpoint = extractAttribute(element.attributes, "destination");
    return telegram;
}

}  // namespace

ParsedTrdpConfig TrdpXmlLoader::parse(const std::string &xml_content) const {
    auto trimmed = trimCopy(xml_content);
    if (trimmed.empty()) {
        throw TrdpXmlLoaderError("XML content is empty");
    }

    ParsedTrdpConfig config;
    auto device_elements = extractElements(trimmed, "device");
    if (device_elements.empty()) {
        auto device_config = extractElements(trimmed, "device-configuration");
        if (!device_config.empty()) {
            auto nested = extractElements(device_config.front().body, "device");
            if (!nested.empty()) {
                device_elements = nested;
            }
        }
    }
    if (!device_elements.empty()) {
        const auto &device = device_elements.front();
        config.device.name = extractAttribute(device.attributes, "name");
        config.device.description = extractAttribute(device.attributes, "description");
    }

    auto dataset_elements = extractElements(trimmed, "dataset");
    for (const auto &element : dataset_elements) {
        ParsedDataset dataset;
        dataset.dataset_id = safeStoi(extractAttribute(element.attributes, "dataset-id"));
        if (dataset.dataset_id <= 0) {
            dataset.dataset_id = safeStoi(extractAttribute(element.attributes, "id"));
        }
        dataset.com_id = safeStoi(extractAttribute(element.attributes, "com-id"));
        dataset.name = extractAttribute(element.attributes, "name");
        if (dataset.dataset_id > 0 || dataset.com_id > 0 || !dataset.name.empty()) {
            config.datasets.push_back(dataset);
        }
    }

    auto interface_elements = extractElements(trimmed, "bus-interface");
    if (interface_elements.empty()) {
        interface_elements = extractElements(trimmed, "interface");
    }
    for (const auto &element : interface_elements) {
        auto iface = parseInterfaceElement(element);
        if (!iface.telegrams.empty()) {
            config.interfaces.push_back(std::move(iface));
        }
    }

    if (!config.hasStructuredTelegrams()) {
        ParsedInterfaceConfig legacy_iface;
        legacy_iface.name = "legacy";
        auto pd_elements = extractElements(trimmed, "pd");
        for (const auto &element : pd_elements) {
            legacy_iface.telegrams.push_back(parseLegacyPd(element));
        }
        auto md_elements = extractElements(trimmed, "md");
        for (const auto &element : md_elements) {
            legacy_iface.telegrams.push_back(parseLegacyMd(element));
        }
        if (!legacy_iface.telegrams.empty()) {
            config.interfaces.push_back(std::move(legacy_iface));
            config.usesLegacyFallback = true;
        }
    }

    if (!config.hasStructuredTelegrams()) {
        throw TrdpXmlLoaderError("No TRDP telegram definitions were found in the XML document");
    }

    return config;
}

}  // namespace trdp::xml
