#pragma once

namespace trdp::http {

// TODO: Implement REST endpoint registration and middleware handling here.
class HttpRouter {
public:
    HttpRouter() = default;
    void registerRoutes();
};

}  // namespace trdp::http
