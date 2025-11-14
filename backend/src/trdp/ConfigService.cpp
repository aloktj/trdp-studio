#include "trdp/ConfigService.hpp"

#include <sqlite3.h>

#include <optional>
#include <string>

#include "auth/AuthManager.hpp"
#include "db/Database.hpp"
#include "httplib.h"

namespace trdp::config {
namespace {
std::optional<long long> parseId(const httplib::Request &req) {
    if (req.matches.size() < 2) {
        return std::nullopt;
    }
    try {
        return std::stoll(req.matches[1]);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
}  // namespace

ConfigService::ConfigService(db::Database &database, auth::AuthManager &auth_manager)
    : database_(database), auth_manager_(auth_manager) {}

void ConfigService::registerRoutes(httplib::Server &server) {
    server.Get("/api/trdp/configs", [this](const httplib::Request &req, httplib::Response &res) {
        handleListConfigs(req, res);
    });

    server.Post("/api/trdp/configs", [this](const httplib::Request &req, httplib::Response &res) {
        handleCreateConfig(req, res);
    });

    server.Get(R"(/api/trdp/configs/(\d+))", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetConfig(req, res);
    });

    server.Post(R"(/api/trdp/configs/(\d+)/activate)", [this](const httplib::Request &req, httplib::Response &res) {
        handleActivateConfig(req, res);
    });
}

void ConfigService::handleListConfigs(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, name, validation_status, created_at FROM xml_configs WHERE user_id = ? ORDER BY created_at DESC;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(jsonError("failed to query configs"), "application/json");
        return;
    }

    sqlite3_bind_int64(stmt, 1, *user_id);

    std::string payload = "{\"configs\":[";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) {
            payload += ",";
        }
        first = false;
        long long id = sqlite3_column_int64(stmt, 0);
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        const unsigned char *status = sqlite3_column_text(stmt, 2);
        const unsigned char *created_at = sqlite3_column_text(stmt, 3);
        payload += "{\"id\":" + std::to_string(id) +
                   ",\"name\":\"" + escapeJson(name ? reinterpret_cast<const char *>(name) : "") +
                   "\",\"validation_status\":\"" +
                   escapeJson(status ? reinterpret_cast<const char *>(status) : "UNKNOWN") + "\",\"created_at\":\"" +
                   escapeJson(created_at ? reinterpret_cast<const char *>(created_at) : "") + "\"}";
    }
    sqlite3_finalize(stmt);

    payload += "]}";
    res.status = 200;
    res.set_content(payload, "application/json");
}

void ConfigService::handleCreateConfig(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    auto name = extractJsonField(req.body, "name");
    auto xml_content = extractJsonField(req.body, "xml_content");

    if (!name || !xml_content) {
        res.status = 400;
        res.set_content(jsonError("name and xml_content are required"), "application/json");
        return;
    }

    std::string validation_status = runValidationStub(*xml_content);

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO xml_configs (user_id, name, xml_content, validation_status) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(jsonError("failed to prepare insert"), "application/json");
        return;
    }

    sqlite3_bind_int64(stmt, 1, *user_id);
    sqlite3_bind_text(stmt, 2, name->c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, xml_content->c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, validation_status.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.status = 500;
        res.set_content(jsonError("failed to store config"), "application/json");
        return;
    }

    long long new_id = sqlite3_last_insert_rowid(database_.handle());
    std::string payload = "{\"status\":\"created\",\"id\":" + std::to_string(new_id) + "}";
    res.status = 201;
    res.set_content(payload, "application/json");
}

void ConfigService::handleGetConfig(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    auto config_id = parseId(req);
    if (!config_id) {
        res.status = 400;
        res.set_content(jsonError("invalid config id"), "application/json");
        return;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, name, xml_content, validation_status, created_at FROM xml_configs WHERE id = ? AND user_id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(jsonError("failed to query config"), "application/json");
        return;
    }

    sqlite3_bind_int64(stmt, 1, *config_id);
    sqlite3_bind_int64(stmt, 2, *user_id);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        res.status = 404;
        res.set_content(jsonError("config not found"), "application/json");
        return;
    }

    const unsigned char *name = sqlite3_column_text(stmt, 1);
    const unsigned char *xml = sqlite3_column_text(stmt, 2);
    const unsigned char *status = sqlite3_column_text(stmt, 3);
    const unsigned char *created_at = sqlite3_column_text(stmt, 4);

    std::string payload =
        "{\"id\":" + std::to_string(*config_id) +
        ",\"name\":\"" + escapeJson(name ? reinterpret_cast<const char *>(name) : "") +
        "\",\"xml_content\":\"" + escapeJson(xml ? reinterpret_cast<const char *>(xml) : "") +
        "\",\"validation_status\":\"" + escapeJson(status ? reinterpret_cast<const char *>(status) : "UNKNOWN") +
        "\",\"created_at\":\"" + escapeJson(created_at ? reinterpret_cast<const char *>(created_at) : "") + "\"}";

    sqlite3_finalize(stmt);

    res.status = 200;
    res.set_content(payload, "application/json");
}

void ConfigService::handleActivateConfig(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    auto config_id = parseId(req);
    if (!config_id) {
        res.status = 400;
        res.set_content(jsonError("invalid config id"), "application/json");
        return;
    }

    if (!configBelongsToUser(*config_id, *user_id)) {
        res.status = 404;
        res.set_content(jsonError("config not found"), "application/json");
        return;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO active_config (id, xml_config_id) VALUES (1, ?) ON CONFLICT(id) DO UPDATE SET xml_config_id = "
        "excluded.xml_config_id;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(jsonError("failed to prepare activation"), "application/json");
        return;
    }

    sqlite3_bind_int64(stmt, 1, *config_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.status = 500;
        res.set_content(jsonError("failed to activate config"), "application/json");
        return;
    }

    res.status = 200;
    res.set_content("{\"status\":\"activated\",\"config_id\":" + std::to_string(*config_id) + "}",
                    "application/json");
}

std::optional<long long> ConfigService::requireUserId(const httplib::Request &req, httplib::Response &res) {
    auto user = auth_manager_.userFromRequest(req);
    if (!user) {
        res.status = 401;
        res.set_content(jsonError("authentication required"), "application/json");
        return std::nullopt;
    }

    return user->id;
}

bool ConfigService::configBelongsToUser(long long config_id, long long user_id) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT 1 FROM xml_configs WHERE id = ? AND user_id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, config_id);
    sqlite3_bind_int64(stmt, 2, user_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

std::optional<std::string> ConfigService::extractJsonField(const std::string &body, const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    auto colon_pos = body.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    auto start_quote = body.find('"', colon_pos);
    if (start_quote == std::string::npos) {
        return std::nullopt;
    }

    auto end_quote = body.find('"', start_quote + 1);
    if (end_quote == std::string::npos) {
        return std::nullopt;
    }

    return body.substr(start_quote + 1, end_quote - start_quote - 1);
}

std::string ConfigService::escapeJson(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string ConfigService::jsonError(const std::string &message) {
    return std::string{"{\"error\":\""} + escapeJson(message) + "\"}";
}

std::string ConfigService::runValidationStub(const std::string &xml_content) {
    (void)xml_content;
    // TODO: Integrate with the real TRDP validation library once available.
    return "UNKNOWN";
}

}  // namespace trdp::config
