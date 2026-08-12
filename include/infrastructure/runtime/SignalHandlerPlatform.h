#pragma once

#include <memory>

namespace devmanager {

class SignalHandlerPlatform {
public:
    virtual ~SignalHandlerPlatform() = default;

    virtual void install() = 0;
    virtual void uninstall() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<SignalHandlerPlatform> createNativeSignalHandlerPlatform();

}  // namespace devmanager
