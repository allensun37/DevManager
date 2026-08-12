#pragma once

namespace devmanager {

class StopRequestSource {
public:
    virtual ~StopRequestSource() = default;

    [[nodiscard]] virtual bool stopRequested() const noexcept = 0;
    [[nodiscard]] virtual bool forceStopRequested() const noexcept = 0;
};

}  // namespace devmanager
