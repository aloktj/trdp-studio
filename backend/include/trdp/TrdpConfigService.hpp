#pragma once

#include <optional>
#include <string>
#include <vector>

namespace trdp::db {
class Database;
}

namespace trdp::config {

struct TrdpConfig {
    long long id {0};
    long long user_id {0};
    std::string name;
    std::string xml_content;
    std::string validation_status;
    std::string created_at;
};

class TrdpConfigService {
public:
    explicit TrdpConfigService(db::Database &database);

    std::vector<TrdpConfig> listConfigsForUser(long long user_id);
    TrdpConfig createConfig(long long user_id, const std::string &name, const std::string &xml_content);
    std::optional<TrdpConfig> getConfigById(long long id);
    void setActiveConfig(long long config_id);

private:
    std::string validateXml(const std::string &xml_content);

    db::Database &database_;
};

}  // namespace trdp::config
