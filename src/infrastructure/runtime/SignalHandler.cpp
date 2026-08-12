#include "infrastructure/runtime/SignalHandler.h"

#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace devmanager {

#ifdef _WIN32
volatile long SignalHandler::stopRequestedFlag_ = 0;
volatile long SignalHandler::forceStopRequestedFlag_ = 0;
#else
volatile std::sig_atomic_t SignalHandler::stopRequestedFlag_ = 0;
volatile std::sig_atomic_t SignalHandler::forceStopRequestedFlag_ = 0;
#endif
std::atomic<SignalHandler*> SignalHandler::installedHandler_ {nullptr};

class NativeSignalHandlerPlatform final : public SignalHandlerPlatform {
public:
    void install() override {
#ifdef _WIN32
        if (::SetConsoleCtrlHandler(&NativeSignalHandlerPlatform::consoleControlHandler, TRUE) == FALSE) {
            throw std::runtime_error("failed to install console stop handler");
        }
#else
        previousSigint_ = std::signal(SIGINT, &NativeSignalHandlerPlatform::posixSignalHandler);
        if (previousSigint_ == SIG_ERR) {
            throw std::runtime_error("failed to install SIGINT stop handler");
        }

        previousSigterm_ = std::signal(SIGTERM, &NativeSignalHandlerPlatform::posixSignalHandler);
        if (previousSigterm_ == SIG_ERR) {
            static_cast<void>(std::signal(SIGINT, previousSigint_));
            throw std::runtime_error("failed to install SIGTERM stop handler");
        }
#endif
        installed_ = true;
    }

    void uninstall() noexcept override {
        if (!installed_) {
            return;
        }

#ifdef _WIN32
        static_cast<void>(::SetConsoleCtrlHandler(&NativeSignalHandlerPlatform::consoleControlHandler, FALSE));
#else
        static_cast<void>(std::signal(SIGINT, previousSigint_));
        static_cast<void>(std::signal(SIGTERM, previousSigterm_));
#endif
        installed_ = false;
    }

private:
#ifdef _WIN32
    static BOOL WINAPI consoleControlHandler(DWORD controlType) noexcept {
        if (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT) {
            SignalHandler::recordStopRequest();
            return TRUE;
        }
        return FALSE;
    }
#else
    static void posixSignalHandler(int signalNumber) noexcept {
        if (signalNumber == SIGINT || signalNumber == SIGTERM) {
            SignalHandler::recordStopRequest();
        }
    }

    using PosixHandler = void (*)(int);
    PosixHandler previousSigint_ {SIG_DFL};
    PosixHandler previousSigterm_ {SIG_DFL};
#endif

    bool installed_ {false};
};

std::unique_ptr<SignalHandlerPlatform> createNativeSignalHandlerPlatform() {
    return std::make_unique<NativeSignalHandlerPlatform>();
}

SignalHandler::SignalHandler()
    : SignalHandler(createNativeSignalHandlerPlatform()) {}

SignalHandler::SignalHandler(std::unique_ptr<SignalHandlerPlatform> platform)
    : platform_(std::move(platform)) {
    if (platform_ == nullptr) {
        throw std::invalid_argument("signal handler platform must not be null");
    }
}

SignalHandler::~SignalHandler() {
    uninstall();
}

void SignalHandler::install() {
    if (installed_) {
        return;
    }

    SignalHandler* expected = nullptr;
    if (!installedHandler_.compare_exchange_strong(expected, this)) {
        throw std::logic_error("a signal handler is already installed");
    }

#ifdef _WIN32
    ::InterlockedExchange(&stopRequestedFlag_, 0L);
    ::InterlockedExchange(&forceStopRequestedFlag_, 0L);
#else
    stopRequestedFlag_ = 0;
    forceStopRequestedFlag_ = 0;
#endif
    try {
        platform_->install();
    } catch (...) {
        installedHandler_.store(nullptr);
        throw;
    }
    installed_ = true;
}

void SignalHandler::uninstall() noexcept {
    if (!installed_) {
        return;
    }

    platform_->uninstall();
    installed_ = false;
    SignalHandler* expected = this;
    static_cast<void>(installedHandler_.compare_exchange_strong(expected, nullptr));
}

bool SignalHandler::stopRequested() const noexcept {
#ifdef _WIN32
    return ::InterlockedCompareExchange(&stopRequestedFlag_, 0L, 0L) != 0L;
#else
    return stopRequestedFlag_ != 0;
#endif
}

bool SignalHandler::forceStopRequested() const noexcept {
#ifdef _WIN32
    return ::InterlockedCompareExchange(&forceStopRequestedFlag_, 0L, 0L) != 0L;
#else
    return forceStopRequestedFlag_ != 0;
#endif
}

void SignalHandler::recordStopRequestForTest() noexcept {
    recordStopRequest();
}

void SignalHandler::resetStopRequestsForTest() noexcept {
#ifdef _WIN32
    ::InterlockedExchange(&stopRequestedFlag_, 0L);
    ::InterlockedExchange(&forceStopRequestedFlag_, 0L);
#else
    stopRequestedFlag_ = 0;
    forceStopRequestedFlag_ = 0;
#endif
}

void SignalHandler::recordStopRequest() noexcept {
#ifdef _WIN32
    if (::InterlockedCompareExchange(&stopRequestedFlag_, 1L, 0L) == 0L) {
        return;
    }
    ::InterlockedExchange(&forceStopRequestedFlag_, 1L);
#else
    if (stopRequestedFlag_ == 0) {
        stopRequestedFlag_ = 1;
        return;
    }
    forceStopRequestedFlag_ = 1;
#endif
}

}  // namespace devmanager
