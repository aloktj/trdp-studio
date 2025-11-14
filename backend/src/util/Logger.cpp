#include "util/Logger.hpp"

#include <iostream>

namespace trdp::util {

void Logger::log(const std::string &message) {
    // TODO: Forward logs to SQLite, files, and remote monitoring sinks.
    std::cout << "Logger stub: " << message << std::endl;
}

}  // namespace trdp::util
