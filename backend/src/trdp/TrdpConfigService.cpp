#include "trdp/TrdpConfigService.hpp"

#include "trdp/TrdpXmlParser.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "db/Database.hpp"
#include "trdp/xml/TrdpXmlLoader.hpp"

namespace trdp::config {
namespace {
TrdpConfig rowToConfig(sqlite3_stmt *stmt) {
    TrdpConfig config;
    config.id = sqlite3_column_int64(stmt, 0);
    config.user_id = sqlite3_column_int64(stmt, 1);
    const unsigned char *name = sqlite3_column_text(stmt, 2);
    const unsigned char *xml = sqlite3_column_text(stmt, 3);
    const unsigned char *status = sqlite3_column_text(stmt, 4);
    const unsigned char *created = sqlite3_column_text(stmt, 5);
    if (name) {
        config.name = reinterpret_cast<const char *>(name);
    }
    if (xml) {
        config.xml_content = reinterpret_cast<const char *>(xml);
    }
    if (status) {
        config.validation_status = reinterpret_cast<const char *>(status);
    }
    if (created) {
        config.created_at = reinterpret_cast<const char *>(created);
    }
    return config;
}
}  // namespace

TrdpConfigService::TrdpConfigService(db::Database &database) : database_(database) {}

std::vector<TrdpConfig> TrdpConfigService::listConfigsForUser(long long user_id) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, user_id, name, xml_content, validation_status, created_at FROM xml_configs WHERE user_id = ? ORDER BY created_at DESC;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to query configs");
    }

    sqlite3_bind_int64(stmt, 1, user_id);

    std::vector<TrdpConfig> configs;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        configs.push_back(rowToConfig(stmt));
    }

    sqlite3_finalize(stmt);
    return configs;
}

TrdpConfig TrdpConfigService::createConfig(long long user_id, const std::string &name, const std::string &xml_content) {
    std::string validation_status = validateXml(xml_content);

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO xml_configs (user_id, name, xml_content, validation_status) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare insert");
    }

    sqlite3_bind_int64(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, xml_content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, validation_status.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("failed to insert config");
    }

    long long new_id = sqlite3_last_insert_rowid(database_.handle());

    TrdpConfig config;
    config.id = new_id;
    config.user_id = user_id;
    config.name = name;
    config.xml_content = xml_content;
    config.validation_status = validation_status;
    config.created_at = "";
    auto full = getConfigById(new_id);
    if (full) {
        return *full;
    }
    return config;
}

std::optional<TrdpConfig> TrdpConfigService::getConfigById(long long id) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, user_id, name, xml_content, validation_status, created_at FROM xml_configs WHERE id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare select");
    }

    sqlite3_bind_int64(stmt, 1, id);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    TrdpConfig config = rowToConfig(stmt);
    sqlite3_finalize(stmt);
    return config;
}

std::optional<TrdpConfig> TrdpConfigService::getActiveConfig() {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT xc.id, xc.user_id, xc.name, xc.xml_content, xc.validation_status, xc.created_at "
        "FROM active_config ac JOIN xml_configs xc ON ac.xml_config_id = xc.id WHERE ac.id = 1 LIMIT 1;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to query active config");
    }

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    TrdpConfig config = rowToConfig(stmt);
    sqlite3_finalize(stmt);
    return config;
}

void TrdpConfigService::setActiveConfig(long long config_id) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO active_config (id, xml_config_id) VALUES (1, ?) ON CONFLICT(id) DO UPDATE SET xml_config_id = excluded.xml_config_id;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare active_config update");
    }

    sqlite3_bind_int64(stmt, 1, config_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("failed to update active config");
    }
}

std::string TrdpConfigService::validateXml(const std::string &xml_content) {
    if (xml_content.empty()) {
        return "XML document is empty";
    }
    if (config::looksLikeTrdpXml(xml_content)) {
        std::string error;
        if (!config::parseTrdpXmlConfig(xml_content, &error)) {
            return error.empty() ? "Failed to parse TRDP XML" : error;
        }
        return "PASS";
    }
    if (xml_content.find("<pd") == std::string::npos && xml_content.find("<md") == std::string::npos) {
        return "Document must declare <pd>/<md> blocks or TRDP <bus-interface> entries";
    }
    return "PASS";
}

}  // namespace trdp::config
