#pragma once

#include "application/StopRequestSource.h"

#include <csignal>

namespace devmanager {

class SignalHandler final : public StopRequestSource {
public:
    SignalHandler() noexcept;
    ~SignalHandler() override;

    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;

    void install();
    void uninstall() noexcept;

    [[nodiscard]] bool stopRequested() const noexcept override;
    [[nodiscard]] bool forceStopRequested() const noexcept override;

    void recordStopRequestForTest() noexcept;

private:
    static void recordStopRequest() noexcept;

#ifdef _WIN32
    static long __stdcall consoleControlHandler(unsigned long controlType) noexcept;
#else
    static void posixSignalHandler(int signalNumber) noexcept;
    using PosixHandler = void (*)(int);
    PosixHandler previousSigint_ {SIG_DFL};
    PosixHandler previousSigterm_ {SIG_DFL};
#endif

    bool installed_ {false};

    static volatile std::sig_atomic_t stopRequestedFlag_;
    static volatile std::sig_atomic_t forceStopRequestedFlag_;
};

}  // namespace devmanager
