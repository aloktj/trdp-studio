#include "network/NetworkConfigService.hpp"

#include <sqlite3.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "db/Database.hpp"

namespace trdp::network {
namespace {
constexpr const char *kSelectSql =
    "SELECT interface_name, local_ip, multicast_groups, pd_port, md_port FROM network_config WHERE id = 1 LIMIT 1;";
constexpr const char *kUpsertSql =
    "INSERT INTO network_config (id, interface_name, local_ip, multicast_groups, pd_port, md_port, updated_at) "
    "VALUES (1, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
    "ON CONFLICT(id) DO UPDATE SET interface_name=excluded.interface_name, "
    "local_ip=excluded.local_ip, multicast_groups=excluded.multicast_groups, "
    "pd_port=excluded.pd_port, md_port=excluded.md_port, updated_at=CURRENT_TIMESTAMP;";
}

NetworkConfigService::NetworkConfigService(db::Database &database) : database_(database) {}

std::optional<NetworkConfig> NetworkConfigService::loadConfig() {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(database_.handle(), kSelectSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare network_config select");
    }

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    NetworkConfig config;
    const unsigned char *iface = sqlite3_column_text(stmt, 0);
    const unsigned char *local_ip = sqlite3_column_text(stmt, 1);
    const unsigned char *groups = sqlite3_column_text(stmt, 2);
    config.pd_port = sqlite3_column_int(stmt, 3);
    config.md_port = sqlite3_column_int(stmt, 4);

    if (iface) {
        config.interface_name = reinterpret_cast<const char *>(iface);
    }
    if (local_ip) {
        config.local_ip = reinterpret_cast<const char *>(local_ip);
    }
    if (groups) {
        config.multicast_groups = splitGroups(reinterpret_cast<const char *>(groups));
    }

    sqlite3_finalize(stmt);
    return config;
}

NetworkConfig NetworkConfigService::saveConfig(const NetworkConfig &config) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(database_.handle(), kUpsertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare network_config upsert");
    }

    std::string stored_groups = joinGroups(config.multicast_groups);

    sqlite3_bind_text(stmt, 1, config.interface_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, config.local_ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, stored_groups.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, config.pd_port);
    sqlite3_bind_int(stmt, 5, config.md_port);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("failed to persist network config");
    }

    auto stored = loadConfig();
    if (stored) {
        return *stored;
    }
    return config;
}

std::string NetworkConfigService::joinGroups(const std::vector<std::string> &groups) {
    std::ostringstream oss;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (i != 0) {
            oss << ",";
        }
        oss << groups[i];
    }
    return oss.str();
}

std::vector<std::string> NetworkConfigService::splitGroups(const std::string &stored) {
    std::vector<std::string> values;
    std::string current;
    for (char ch : stored) {
        if (ch == ',') {
            if (!current.empty()) {
                values.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        values.push_back(current);
    }
    return values;
}

}  // namespace trdp::network
