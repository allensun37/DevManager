#include "infrastructure/runtime/SignalHandler.h"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace devmanager {

volatile std::sig_atomic_t SignalHandler::stopRequestedFlag_ = 0;
volatile std::sig_atomic_t SignalHandler::forceStopRequestedFlag_ = 0;

SignalHandler::SignalHandler() noexcept {
    stopRequestedFlag_ = 0;
    forceStopRequestedFlag_ = 0;
}

SignalHandler::~SignalHandler() {
    uninstall();
}

void SignalHandler::install() {
    if (installed_) {
        return;
    }

#ifdef _WIN32
    if (::SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(&SignalHandler::consoleControlHandler), TRUE) == FALSE) {
        throw std::runtime_error("failed to install console stop handler");
    }
#else
    previousSigint_ = std::signal(SIGINT, &SignalHandler::posixSignalHandler);
    if (previousSigint_ == SIG_ERR) {
        throw std::runtime_error("failed to install SIGINT stop handler");
    }

    previousSigterm_ = std::signal(SIGTERM, &SignalHandler::posixSignalHandler);
    if (previousSigterm_ == SIG_ERR) {
        static_cast<void>(std::signal(SIGINT, previousSigint_));
        throw std::runtime_error("failed to install SIGTERM stop handler");
    }
#endif

    installed_ = true;
}

void SignalHandler::uninstall() noexcept {
    if (!installed_) {
        return;
    }

#ifdef _WIN32
    static_cast<void>(::SetConsoleCtrlHandler(
        reinterpret_cast<PHANDLER_ROUTINE>(&SignalHandler::consoleControlHandler), FALSE));
#else
    static_cast<void>(std::signal(SIGINT, previousSigint_));
    static_cast<void>(std::signal(SIGTERM, previousSigterm_));
#endif

    installed_ = false;
}

bool SignalHandler::stopRequested() const noexcept {
    return stopRequestedFlag_ != 0;
}

bool SignalHandler::forceStopRequested() const noexcept {
    return forceStopRequestedFlag_ != 0;
}

void SignalHandler::recordStopRequestForTest() noexcept {
    recordStopRequest();
}

void SignalHandler::recordStopRequest() noexcept {
    if (stopRequestedFlag_ == 0) {
        stopRequestedFlag_ = 1;
        return;
    }
    forceStopRequestedFlag_ = 1;
}

#ifdef _WIN32
long __stdcall SignalHandler::consoleControlHandler(unsigned long controlType) noexcept {
    if (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT) {
        recordStopRequest();
        return TRUE;
    }
    return FALSE;
}
#else
void SignalHandler::posixSignalHandler(int signalNumber) noexcept {
    if (signalNumber == SIGINT || signalNumber == SIGTERM) {
        recordStopRequest();
    }
}
#endif

}  // namespace devmanager
