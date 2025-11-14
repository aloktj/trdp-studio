#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "network/NetworkConfigService.hpp"
#include "trdp/TrdpConfigService.hpp"

namespace trdp::stack {

struct PdMessage {
    int id {0};
    std::string name;
    std::vector<uint8_t> payload;
    std::string timestamp;
};

struct MdMessage {
    int id {0};
    std::string source;
    std::string destination;
    std::vector<uint8_t> payload;
    std::string timestamp;
};

class TrdpEngine {
public:
    TrdpEngine();

    bool loadConfiguration(const config::TrdpConfig &config, const network::NetworkConfig &net_cfg);
    void start();
    void stop();

    std::vector<PdMessage> listOutgoingPd() const;
    std::vector<PdMessage> listIncomingPd() const;
    void updateOutgoingPdPayload(int msg_id, const std::vector<uint8_t> &payload);

    std::vector<MdMessage> listOutgoingMd() const;
    std::vector<MdMessage> listIncomingMd() const;
    MdMessage sendMdMessage(const std::string &destination, const std::vector<uint8_t> &payload);

private:
    void seedDemoDataIfNeeded();
    static std::string nowIso8601();

    bool running_ {false};
    std::optional<config::TrdpConfig> loaded_config_;
    std::optional<network::NetworkConfig> network_config_;
    std::vector<PdMessage> outgoing_pd_;
    std::vector<PdMessage> incoming_pd_;
    std::vector<MdMessage> outgoing_md_;
    std::vector<MdMessage> incoming_md_;
    int next_pd_id_ {1};
    int next_md_id_ {1};
};

}  // namespace trdp::stack
