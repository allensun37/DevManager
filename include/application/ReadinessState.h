#pragma once

#include <atomic>

namespace devmanager {

enum class ServiceState {
    Starting,
    Ready,
    Stopping,
    Stopped,
    Failed,
};

class ReadinessState final {
public:
    [[nodiscard]] ServiceState state() const noexcept;
    [[nodiscard]] bool isReady() const noexcept;

    void markReady() noexcept;
    void markStopping() noexcept;
    void markStopped() noexcept;
    void markFailed() noexcept;

private:
    void transitionTo(ServiceState next) noexcept;

    std::atomic<ServiceState> state_ {ServiceState::Starting};
};

}  // namespace devmanager
