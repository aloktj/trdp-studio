#include "util/LogService.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>

#include "db/Database.hpp"

namespace trdp::util {

namespace {

std::string toUpperCopy(const std::string &value) {
    std::string copy = value;
    std::transform(copy.begin(), copy.end(), copy.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return copy;
}

int sanitizeLimit(int limit) {
    if (limit <= 0) {
        return 1;
    }
    if (limit > 500) {
        return 500;
    }
    return limit;
}

int sanitizeOffset(int offset) {
    if (offset < 0) {
        return 0;
    }
    return offset;
}

}  // namespace

LogService::LogService(db::Database &database) : database_(database) {}

std::vector<TrdpLogEntry> LogService::getTrdpLogs(int limit, int offset, std::optional<std::string> type_filter,
                                                  std::optional<std::string> direction_filter) {
    sqlite3 *db = database_.handle();
    if (db == nullptr) {
        return {};
    }

    std::string sql =
        "SELECT id, direction, type, msg_id, src_ip, dst_ip, payload, timestamp FROM trdp_logs";
    std::vector<std::string> clauses;
    std::vector<std::string> values;

    if (type_filter && !type_filter->empty()) {
        auto value = toUpperCopy(*type_filter);
        if (value == "PD" || value == "MD") {
            clauses.push_back("type = ?");
            values.push_back(value);
        }
    }
    if (direction_filter && !direction_filter->empty()) {
        auto value = toUpperCopy(*direction_filter);
        if (value == "IN" || value == "OUT") {
            clauses.push_back("direction = ?");
            values.push_back(value);
        }
    }

    if (!clauses.empty()) {
        sql += " WHERE ";
        for (size_t i = 0; i < clauses.size(); ++i) {
            if (i != 0) {
                sql += " AND ";
            }
            sql += clauses[i];
        }
    }

    sql += " ORDER BY id DESC LIMIT ? OFFSET ?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error{"Failed to prepare TRDP log query"};
    }

    int param_index = 1;
    for (const auto &value : values) {
        sqlite3_bind_text(stmt, param_index++, value.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, param_index++, sanitizeLimit(limit));
    sqlite3_bind_int(stmt, param_index++, sanitizeOffset(offset));

    std::vector<TrdpLogEntry> logs;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TrdpLogEntry entry;
        entry.id = sqlite3_column_int(stmt, 0);
        const unsigned char *dir = sqlite3_column_text(stmt, 1);
        const unsigned char *type = sqlite3_column_text(stmt, 2);
        entry.direction = dir ? reinterpret_cast<const char *>(dir) : "";
        entry.type = type ? reinterpret_cast<const char *>(type) : "";
        entry.msg_id = sqlite3_column_int(stmt, 3);
        const unsigned char *src = sqlite3_column_text(stmt, 4);
        const unsigned char *dst = sqlite3_column_text(stmt, 5);
        entry.src_ip = src ? reinterpret_cast<const char *>(src) : "";
        entry.dst_ip = dst ? reinterpret_cast<const char *>(dst) : "";
        const void *blob = sqlite3_column_blob(stmt, 6);
        int blob_size = sqlite3_column_bytes(stmt, 6);
        if (blob != nullptr && blob_size > 0) {
            const auto *data = static_cast<const uint8_t *>(blob);
            entry.payload.assign(data, data + blob_size);
        } else {
            entry.payload.clear();
        }
        const unsigned char *ts = sqlite3_column_text(stmt, 7);
        entry.timestamp = ts ? reinterpret_cast<const char *>(ts) : "";
        logs.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    return logs;
}

std::vector<AppLogEntry> LogService::getAppLogs(int limit, int offset, std::optional<std::string> level_filter) {
    sqlite3 *db = database_.handle();
    if (db == nullptr) {
        return {};
    }

    std::string sql = "SELECT id, level, message, timestamp FROM app_logs";
    bool has_filter = false;
    std::string normalized_level;
    if (level_filter && !level_filter->empty()) {
        normalized_level = toUpperCopy(*level_filter);
        has_filter = true;
        sql += " WHERE UPPER(level) = ?";
    }
    sql += " ORDER BY id DESC LIMIT ? OFFSET ?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error{"Failed to prepare app log query"};
    }

    int param_index = 1;
    if (has_filter) {
        sqlite3_bind_text(stmt, param_index++, normalized_level.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, param_index++, sanitizeLimit(limit));
    sqlite3_bind_int(stmt, param_index++, sanitizeOffset(offset));

    std::vector<AppLogEntry> logs;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AppLogEntry entry;
        entry.id = sqlite3_column_int(stmt, 0);
        const unsigned char *level = sqlite3_column_text(stmt, 1);
        const unsigned char *message = sqlite3_column_text(stmt, 2);
        const unsigned char *timestamp = sqlite3_column_text(stmt, 3);
        entry.level = level ? reinterpret_cast<const char *>(level) : "";
        entry.message = message ? reinterpret_cast<const char *>(message) : "";
        entry.timestamp = timestamp ? reinterpret_cast<const char *>(timestamp) : "";
        logs.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    return logs;
}

}  // namespace trdp::util
