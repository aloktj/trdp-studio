#pragma once

namespace trdp::stack {

// TODO: Connect to the TRDP static library and manage PD/MD sessions.
class TrdpEngine {
public:
    TrdpEngine() = default;
    void initialize();
};

}  // namespace trdp::stack
