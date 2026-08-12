#pragma once

#include "application/StopRequestSource.h"
#include "infrastructure/runtime/SignalHandlerPlatform.h"

#include <csignal>
#include <atomic>
#include <memory>

namespace devmanager {

class SignalHandler final : public StopRequestSource {
public:
    SignalHandler();
    explicit SignalHandler(std::unique_ptr<SignalHandlerPlatform> platform);
    ~SignalHandler() override;

    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;

    void install();
    void uninstall() noexcept;

    [[nodiscard]] bool stopRequested() const noexcept override;
    [[nodiscard]] bool forceStopRequested() const noexcept override;

    void recordStopRequestForTest() noexcept;
    void resetStopRequestsForTest() noexcept;

private:
    static void recordStopRequest() noexcept;

    friend class NativeSignalHandlerPlatform;

    std::unique_ptr<SignalHandlerPlatform> platform_;
    bool installed_ {false};

    static std::atomic<SignalHandler*> installedHandler_;

#ifdef _WIN32
    static volatile long stopRequestedFlag_;
    static volatile long forceStopRequestedFlag_;
#else
    static volatile std::sig_atomic_t stopRequestedFlag_;
    static volatile std::sig_atomic_t forceStopRequestedFlag_;
#endif
};

}  // namespace devmanager
