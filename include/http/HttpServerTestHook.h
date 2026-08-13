#pragma once

namespace devmanager {

class HttpServerTestHook {
public:
    virtual ~HttpServerTestHook() = default;

    virtual void onAuthenticatedRequest() noexcept = 0;
};

}  // namespace devmanager
