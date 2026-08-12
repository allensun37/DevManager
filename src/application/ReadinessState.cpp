#include "application/ReadinessState.h"

namespace devmanager {

ServiceState ReadinessState::state() const noexcept {
    return state_.load();
}

bool ReadinessState::isReady() const noexcept {
    return state() == ServiceState::Ready;
}

void ReadinessState::markReady() noexcept {
    transitionTo(ServiceState::Ready);
}

void ReadinessState::markStopping() noexcept {
    transitionTo(ServiceState::Stopping);
}

void ReadinessState::markStopped() noexcept {
    transitionTo(ServiceState::Stopped);
}

void ReadinessState::markFailed() noexcept {
    transitionTo(ServiceState::Failed);
}

void ReadinessState::transitionTo(ServiceState next) noexcept {
    ServiceState current = state_.load();
    while (current != ServiceState::Stopped && current != ServiceState::Failed) {
        if (state_.compare_exchange_weak(current, next)) {
            return;
        }
    }
}

}  // namespace devmanager
