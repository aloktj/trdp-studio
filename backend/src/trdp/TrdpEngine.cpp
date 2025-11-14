#include "trdp/TrdpEngine.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <stdexcept>

#include "network/NetworkConfigService.hpp"
#include "trdp/TrdpConfigService.hpp"

namespace trdp::stack {

TrdpEngine::TrdpEngine() { seedDemoDataIfNeeded(); }

bool TrdpEngine::loadConfiguration(const config::TrdpConfig &config, const network::NetworkConfig &net_cfg) {
    loaded_config_ = config;
    network_config_ = net_cfg;
    // TODO: Wire up the actual TRDP stack initialization when the vendor library is available.
    return true;
}

void TrdpEngine::start() {
    running_ = true;
    // TODO: Kick off PD/MD communication threads once the TRDP engine is integrated.
}

void TrdpEngine::stop() {
    running_ = false;
    // TODO: Gracefully stop PD/MD communication threads.
}

std::vector<PdMessage> TrdpEngine::listOutgoingPd() const { return outgoing_pd_; }

std::vector<PdMessage> TrdpEngine::listIncomingPd() const { return incoming_pd_; }

void TrdpEngine::updateOutgoingPdPayload(int msg_id, const std::vector<uint8_t> &payload) {
    auto it = std::find_if(outgoing_pd_.begin(), outgoing_pd_.end(), [msg_id](const PdMessage &msg) {
        return msg.id == msg_id;
    });

    if (it == outgoing_pd_.end()) {
        throw std::runtime_error("PD message not found");
    }

    it->payload = payload;
    it->timestamp = nowIso8601();
}

std::vector<MdMessage> TrdpEngine::listOutgoingMd() const { return outgoing_md_; }

std::vector<MdMessage> TrdpEngine::listIncomingMd() const { return incoming_md_; }

MdMessage TrdpEngine::sendMdMessage(const std::string &destination, const std::vector<uint8_t> &payload) {
    MdMessage message;
    message.id = next_md_id_++;
    message.source = "UI";
    message.destination = destination;
    message.payload = payload;
    message.timestamp = nowIso8601();

    outgoing_md_.push_back(message);
    return message;
}

void TrdpEngine::seedDemoDataIfNeeded() {
    if (!outgoing_pd_.empty() || !incoming_pd_.empty()) {
        return;
    }

    PdMessage speed;
    speed.id = next_pd_id_++;
    speed.name = "TrainSpeed";
    speed.payload = {0x00, 0x2A};
    speed.timestamp = nowIso8601();

    PdMessage temp;
    temp.id = next_pd_id_++;
    temp.name = "CoachTemperature";
    temp.payload = {0x00, 0x19};
    temp.timestamp = nowIso8601();

    outgoing_pd_.push_back(speed);
    outgoing_pd_.push_back(temp);

    PdMessage incoming;
    incoming.id = next_pd_id_++;
    incoming.name = "BrakeStatus";
    incoming.payload = {0x01, 0x00};
    incoming.timestamp = nowIso8601();

    incoming_pd_.push_back(incoming);

    MdMessage alarm;
    alarm.id = next_md_id_++;
    alarm.source = "TrainControl";
    alarm.destination = "UI";
    alarm.payload = {0xDE, 0xAD, 0xBE, 0xEF};
    alarm.timestamp = nowIso8601();
    incoming_md_.push_back(alarm);
}

std::string TrdpEngine::nowIso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&time);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

}  // namespace trdp::stack
