#include "application/ServiceLifecycle.h"

#include "infrastructure/logging/Logger.h"

namespace devmanager {

ServiceLifecycle::ServiceLifecycle(ReadinessState& readiness,
                                   StopRequestSource& stopSource,
                                   ServiceRuntime& runtime,
                                   Logger& logger,
                                   std::chrono::milliseconds drainDeadline)
    : readiness_(readiness),
      stopSource_(stopSource),
      runtime_(runtime),
      logger_(logger),
      drainDeadline_(drainDeadline) {}

void ServiceLifecycle::markReady() noexcept {
    readiness_.markReady();
}

ServiceExit ServiceLifecycle::stopWhenRequested() {
    if (readiness_.state() == ServiceState::Failed) {
        return ServiceExit::Failed;
    }
    if (stoppingStarted_) {
        return stoppingResult_;
    }
    if (!stopSource_.stopRequested()) {
        return ServiceExit::NoStopRequested;
    }

    stoppingStarted_ = true;
    readiness_.markStopping();
    runtime_.stopAccepting();
    logger_.info("shutdown_requested");

    if (stopSource_.forceStopRequested()) {
        logger_.warn("shutdown_force_requested");
        readiness_.markStopped();
        stoppingResult_ = ServiceExit::ForceStopRequested;
        return stoppingResult_;
    }

    const bool drained = runtime_.waitUntilDrained(drainDeadline_);
    if (stopSource_.forceStopRequested()) {
        logger_.warn("shutdown_force_requested");
        readiness_.markStopped();
        stoppingResult_ = ServiceExit::ForceStopRequested;
        return stoppingResult_;
    }

    if (!drained) {
        logger_.warn("shutdown_timeout");
        stoppingResult_ = ServiceExit::ShutdownTimeout;
        return stoppingResult_;
    }

    readiness_.markStopped();
    stoppingResult_ = ServiceExit::Stopped;
    return stoppingResult_;
}

ServiceExit ServiceLifecycle::finishAfterDrain() {
    if (readiness_.state() == ServiceState::Failed) {
        return ServiceExit::Failed;
    }
    if (stoppingResult_ != ServiceExit::ShutdownTimeout) {
        return stoppingResult_;
    }

    if (runtime_.waitUntilDrained(std::chrono::milliseconds::max())) {
        readiness_.markStopped();
    }
    return ServiceExit::ShutdownTimeout;
}

ServiceExit ServiceLifecycle::markFailed() noexcept {
    if (readiness_.state() == ServiceState::Stopped) {
        return ServiceExit::Stopped;
    }
    readiness_.markFailed();
    return ServiceExit::Failed;
}

}  // namespace devmanager
