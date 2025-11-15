#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace trdp::xml {

enum class TelegramKind { kPd, kMd };

enum class TelegramDirection { kPublisher, kSubscriber };

struct TelegramEndpoint {
    std::string endpoint;
};

struct ParsedDataset {
    int dataset_id {0};
    int com_id {0};
    std::string name;
};

struct ParsedTelegram {
    TelegramKind kind {TelegramKind::kPd};
    TelegramDirection direction {TelegramDirection::kPublisher};
    int com_id {0};
    int dataset_id {0};
    int cycle_time_ms {0};
    std::string name;
    TelegramEndpoint source;
    TelegramEndpoint destination;
    std::vector<uint8_t> default_payload;
};

struct ParsedInterfaceConfig {
    std::string name;
    std::vector<ParsedTelegram> telegrams;
};

struct ParsedDeviceConfig {
    std::string name;
    std::string description;
};

struct ParsedTrdpConfig {
    ParsedDeviceConfig device;
    std::vector<ParsedDataset> datasets;
    std::vector<ParsedInterfaceConfig> interfaces;
    bool usesLegacyFallback {false};

    bool hasStructuredTelegrams() const;
};

class TrdpXmlLoaderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class TrdpXmlLoader {
public:
    ParsedTrdpConfig parse(const std::string &xml_content) const;
};

}  // namespace trdp::xml
