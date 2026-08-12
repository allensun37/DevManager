#pragma once

#include <chrono>

namespace devmanager {

class ServiceRuntime {
public:
    virtual ~ServiceRuntime() = default;

    virtual void stopAccepting() noexcept = 0;
    [[nodiscard]] virtual bool waitUntilDrained(
        std::chrono::milliseconds timeout) noexcept = 0;
};

}  // namespace devmanager
