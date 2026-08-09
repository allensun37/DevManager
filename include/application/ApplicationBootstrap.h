#pragma once

#include "config/Config.h"
#include "application/ProjectManager.h"
#include "application/ProjectService.h"
#include "infrastructure/logging/Logger.h"
#include "repository/ProjectRepository.h"

#include <memory>
#include <utility>

namespace devmanager {

class ApplicationBootstrap final {
public:
    explicit ApplicationBootstrap(Config config);

    [[nodiscard]] const Config& config() const noexcept;
    [[nodiscard]] Logger& logger() noexcept;
    [[nodiscard]] ProjectRepository& repository() noexcept;
    [[nodiscard]] ProjectManager& manager() noexcept;
    [[nodiscard]] ProjectService& service() noexcept;

private:
    Config config_;
    Logger logger_;
    std::unique_ptr<ProjectRepository> repository_;
    ProjectManager manager_;
    ProjectService service_;
};

}  // namespace devmanager
