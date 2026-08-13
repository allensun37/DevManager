#pragma once

#include "application/ReadinessState.h"
#include "application/ServiceRuntime.h"
#include "application/StopRequestSource.h"

#include <chrono>

namespace devmanager {

class Logger;

enum class ServiceExit {
    NoStopRequested,
    Stopped,
    ShutdownTimeout,
    ForceStopRequested,
    Failed,
};

class ServiceLifecycle final {
public:
    ServiceLifecycle(ReadinessState& readiness,
                     StopRequestSource& stopSource,
                     ServiceRuntime& runtime,
                     Logger& logger,
                     std::chrono::milliseconds drainDeadline = std::chrono::seconds(5));

    void markReady() noexcept;
    [[nodiscard]] ServiceExit stopWhenRequested();
    [[nodiscard]] ServiceExit finishAfterDrain();
    [[nodiscard]] ServiceExit markFailed() noexcept;

private:
    ReadinessState& readiness_;
    StopRequestSource& stopSource_;
    ServiceRuntime& runtime_;
    Logger& logger_;
    std::chrono::milliseconds drainDeadline_;
    bool stoppingStarted_ {false};
    ServiceExit stoppingResult_ {ServiceExit::NoStopRequested};
};

}  // namespace devmanager
