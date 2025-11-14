#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace trdp::stack {
struct PdMessage;
struct MdMessage;
}

namespace trdp::util {
struct TrdpLogEntry;
struct AppLogEntry;
}

namespace trdp::http::json {

std::string escape(const std::string &value);
std::string error(const std::string &message);

std::optional<std::string> stringField(const std::string &body, const std::string &field_name);
std::optional<int> intField(const std::string &body, const std::string &field_name);
std::optional<std::vector<std::string>> stringArrayField(const std::string &body,
                                                         const std::string &field_name);
std::optional<std::vector<uint8_t>> parseHex(const std::string &hex);
std::optional<std::vector<uint8_t>> hexToBlob(const std::string &hex);
std::string bytesToHex(const std::vector<uint8_t> &data);
std::string blobToHex(const void *data, size_t size);
std::string payloadAscii(const std::vector<uint8_t> &data);
std::string endpointIp(const std::string &endpoint);

std::string pdListJson(const std::vector<stack::PdMessage> &messages, bool include_cycle_time);
std::string pdDetailJson(const stack::PdMessage &message);
std::string mdIncomingListJson(const std::vector<stack::MdMessage> &messages);
std::string mdSendResponseJson(const stack::MdMessage &message);
std::string trdpLogListJson(const std::vector<util::TrdpLogEntry> &logs);
std::string appLogListJson(const std::vector<util::AppLogEntry> &logs);

}  // namespace trdp::http::json

