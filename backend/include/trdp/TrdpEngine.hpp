#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "network/NetworkConfigService.hpp"
#include "trdp/TrdpConfigService.hpp"

namespace trdp::db {
class Database;
}

namespace trdp::stack {

struct PdMessage {
    int id {0};
    std::string name;
    int cycle_time_ms {0};
    std::vector<uint8_t> payload;
    std::string timestamp;
};

struct MdMessage {
    int id {0};
    int msg_id {0};
    std::string source;
    std::string destination;
    std::vector<uint8_t> payload;
    std::string timestamp;
};

class TrdpEngine {
public:
    explicit TrdpEngine(db::Database *database = nullptr);
    ~TrdpEngine();

    bool loadConfiguration(const config::TrdpConfig &config, const network::NetworkConfig &net_cfg);
    void start();
    void stop();

    std::vector<PdMessage> listOutgoingPd() const;
    std::vector<PdMessage> listIncomingPd() const;
    void updateOutgoingPdPayload(int msg_id, const std::vector<uint8_t> &payload);

    std::vector<MdMessage> listOutgoingMd() const;
    std::vector<MdMessage> listIncomingMd() const;
    MdMessage sendMdMessage(const std::string &destination, const std::vector<uint8_t> &payload);
    MdMessage sendMdMessage(const std::string &destination, int msg_id,
                           const std::vector<uint8_t> &payload);

private:
    struct PdRuntimeState;
    struct MdRuntimeState;
    class TrdpStackAdapter;

    bool initializeStackLocked(const network::NetworkConfig &net_cfg);
    void teardownStackLocked();
    void rebuildStateFromConfig(const std::string &xml_content);
    void runEventLoop();
    void scheduleNextCycle(PdRuntimeState &state);
    void handleIncomingPd(int msg_id, const std::vector<uint8_t> &payload, const std::string &src_ip,
                          const std::string &dst_ip);
    void handleIncomingMd(int msg_id, const std::vector<uint8_t> &payload, const std::string &src_ip,
                          const std::string &dst_ip);
    void logTrdpEvent(const std::string &direction, const std::string &type, int msg_id,
                      const std::string &src_ip, const std::string &dst_ip,
                      const std::vector<uint8_t> &payload);
    static std::string sanitizeEndpoint(const std::string &endpoint);
    static std::string extractIp(const std::string &endpoint);
    static uint16_t extractPort(const std::string &endpoint, uint16_t fallback);
    static void pdCallbackBridge(void *ref_con, const uint8_t *payload, uint32_t size,
                                 const char *src_ip, const char *dst_ip);
    static void mdCallbackBridge(void *ref_con, const uint8_t *payload, uint32_t size,
                                 const char *src_ip, const char *dst_ip);
    void ensureWorker();
    void stopWorker();
    void clearAllStateLocked();
    static std::string nowIso8601();

    bool running_ {false};
    std::atomic<bool> stack_ready_ {false};
    std::optional<config::TrdpConfig> loaded_config_;
    std::optional<network::NetworkConfig> network_config_;
    std::vector<PdMessage> outgoing_pd_;
    std::vector<PdMessage> incoming_pd_;
    std::vector<MdMessage> outgoing_md_;
    std::vector<MdMessage> incoming_md_;
    std::unordered_map<int, size_t> outgoing_pd_index_;
    std::unordered_map<int, size_t> incoming_pd_index_;
    std::unordered_map<int, size_t> outgoing_md_index_;
    std::unordered_map<int, size_t> incoming_md_index_;
    std::unordered_map<int, std::shared_ptr<PdRuntimeState>> pd_runtime_;
    std::unordered_map<int, std::shared_ptr<MdRuntimeState>> md_runtime_;
    int next_pd_id_ {1};
    int next_md_id_ {1};
    int next_md_msg_id_ {1};
    int next_md_runtime_id_ {1};
    db::Database *database_ {nullptr};
    std::unique_ptr<TrdpStackAdapter> stack_adapter_;
    mutable std::mutex state_mutex_;
    std::mutex engine_mutex_;
    std::thread worker_thread_;
    std::atomic<bool> stop_worker_ {true};
};

}  // namespace trdp::stack
