#pragma once

#include <string>

namespace trdp::util {

// TODO: Implement structured logging, log rotation, and log persistence hooks.
class Logger {
public:
    Logger() = default;
    void log(const std::string &message);
};

}  // namespace trdp::util
