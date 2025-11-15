#include "trdp/TrdpXmlParser.hpp"

#include <initializer_list>

#include "trdp/XmlUtils.hpp"

namespace trdp::config {
namespace {

using trdp::xml::XmlElement;
using trdp::xml::extractElements;
using trdp::xml::parseHexPayload;
using trdp::xml::safeStoi;
using trdp::xml::toLowerCopy;
using trdp::xml::trimCopy;

std::string findAttribute(const XmlElement &element, std::initializer_list<const char *> keys) {
    for (const char *key : keys) {
        auto it = element.attributes.find(key);
        if (it != element.attributes.end()) {
            return it->second;
        }
    }
    return "";
}

TrdpTelegramType parseType(const std::string &value) {
    const auto lowered = toLowerCopy(value);
    if (lowered == "md") {
        return TrdpTelegramType::kMd;
    }
    return TrdpTelegramType::kPd;
}

TrdpTelegramDirection parseDirection(const std::string &value, TrdpTelegramType type) {
    const auto lowered = toLowerCopy(value);
    if (lowered == "subscriber" || lowered == "sink" || lowered == "listener") {
        return type == TrdpTelegramType::kMd ? TrdpTelegramDirection::kListener
                                             : TrdpTelegramDirection::kSubscriber;
    }
    if (lowered == "responder" || lowered == "reply") {
        return TrdpTelegramDirection::kResponder;
    }
    return TrdpTelegramDirection::kPublisher;
}

bool hasTrdpMarkers(const std::string &xml_content) {
    const auto lowered = toLowerCopy(xml_content);
    return lowered.find("<device-configuration") != std::string::npos ||
           lowered.find("<bus-interface") != std::string::npos ||
           lowered.find("<telegram") != std::string::npos;
}

}  // namespace

std::optional<TrdpXmlConfig> parseTrdpXmlConfig(const std::string &xml_content, std::string *error_out) {
    TrdpXmlConfig config;
    auto interfaces = extractElements(xml_content, "bus-interface");
    if (interfaces.empty()) {
        if (error_out != nullptr) {
            *error_out = "XML does not declare any <bus-interface> elements";
        }
        return std::nullopt;
    }
    for (auto &iface_element : interfaces) {
        TrdpInterfaceDefinition iface;
        auto it_name = iface_element.attributes.find("name");
        if (it_name != iface_element.attributes.end()) {
            iface.name = trimCopy(it_name->second);
        }
        auto telegrams = extractElements(iface_element.body, "telegram");
        for (auto &telegram_element : telegrams) {
            TrdpTelegramDefinition telegram;
            telegram.name = trimCopy(findAttribute(telegram_element, {"name", "label"}));
            telegram.type = parseType(findAttribute(telegram_element, {"type", "telegram"}));
            telegram.direction =
                parseDirection(findAttribute(telegram_element, {"direction", "role"}), telegram.type);
            telegram.com_id = safeStoi(findAttribute(telegram_element, {"com-id", "comId", "comid"}));
            telegram.cycle_time_ms = safeStoi(findAttribute(telegram_element, {"cycle", "interval", "cycle-time"}));
            telegram.timeout_ms = safeStoi(findAttribute(telegram_element, {"timeout", "watchdog"}));
            telegram.source = trimCopy(findAttribute(telegram_element, {"source", "src", "from"}));
            telegram.destination = trimCopy(findAttribute(telegram_element, {"destination", "dest", "to"}));
            telegram.dataset = trimCopy(findAttribute(telegram_element, {"dataset", "dataset-id", "dataset-ref"}));
            telegram.payload_text = trimCopy(findAttribute(telegram_element, {"payload"}));
            if (telegram.payload_text.empty()) {
                telegram.payload_text = trimCopy(telegram_element.body);
            }
            telegram.payload = parseHexPayload(telegram.payload_text);
            iface.telegrams.push_back(std::move(telegram));
        }
        if (!iface.telegrams.empty()) {
            config.interfaces.push_back(std::move(iface));
        }
    }
    if (config.interfaces.empty()) {
        if (error_out != nullptr) {
            *error_out = "No telegram definitions were found inside <bus-interface> sections";
        }
        return std::nullopt;
    }
    return config;
}

bool looksLikeTrdpXml(const std::string &xml_content) {
    return hasTrdpMarkers(xml_content);
}

}  // namespace trdp::config
