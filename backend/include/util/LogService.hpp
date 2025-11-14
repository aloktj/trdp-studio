#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace trdp::db {
class Database;
}

namespace trdp::util {

struct TrdpLogEntry {
    int id {0};
    std::string direction;
    std::string type;
    int msg_id {0};
    std::string src_ip;
    std::string dst_ip;
    std::vector<uint8_t> payload;
    std::string timestamp;
};

struct AppLogEntry {
    int id {0};
    std::string level;
    std::string message;
    std::string timestamp;
};

class LogService {
public:
    explicit LogService(db::Database &database);

    std::vector<TrdpLogEntry> getTrdpLogs(int limit, int offset,
                                          std::optional<std::string> type_filter,
                                          std::optional<std::string> direction_filter);

    std::vector<AppLogEntry> getAppLogs(int limit, int offset, std::optional<std::string> level_filter);

    void appendAppLog(const std::string &level, const std::string &message);

private:
    db::Database &database_;
};

}  // namespace trdp::util
