#pragma once

#include <optional>
#include <string>
#include <vector>

namespace trdp::db {
class Database;
}

namespace trdp::network {

struct NetworkConfig {
    std::string interface_name;
    std::string local_ip;
    std::vector<std::string> multicast_groups;
    int pd_port {0};
    int md_port {0};
};

class NetworkConfigService {
public:
    explicit NetworkConfigService(db::Database &database);

    std::optional<NetworkConfig> loadConfig();
    NetworkConfig saveConfig(const NetworkConfig &config);

private:
    static std::string joinGroups(const std::vector<std::string> &groups);
    static std::vector<std::string> splitGroups(const std::string &stored);

    db::Database &database_;
};

}  // namespace trdp::network
