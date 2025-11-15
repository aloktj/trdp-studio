#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace trdp::config {

enum class TrdpTelegramType { kPd, kMd };

enum class TrdpTelegramDirection { kPublisher, kSubscriber, kListener, kResponder };

struct TrdpTelegramDefinition {
    TrdpTelegramType type {TrdpTelegramType::kPd};
    TrdpTelegramDirection direction {TrdpTelegramDirection::kPublisher};
    std::string name;
    int com_id {0};
    int cycle_time_ms {0};
    int timeout_ms {0};
    std::string source;
    std::string destination;
    std::string dataset;
    std::string payload_text;
    std::vector<uint8_t> payload;
};

struct TrdpInterfaceDefinition {
    std::string name;
    std::vector<TrdpTelegramDefinition> telegrams;
};

struct TrdpXmlConfig {
    std::vector<TrdpInterfaceDefinition> interfaces;
    bool empty() const { return interfaces.empty(); }
};

std::optional<TrdpXmlConfig> parseTrdpXmlConfig(const std::string &xml_content, std::string *error_out = nullptr);
bool looksLikeTrdpXml(const std::string &xml_content);

}  // namespace trdp::config
