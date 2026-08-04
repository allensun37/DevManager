#pragma once

#include "config/Config.h"
#include "application/ProjectManager.h"
#include "repository/JsonProjectRepository.h"

#include <utility>

namespace devmanager {

class ApplicationBootstrap final {
public:
    explicit ApplicationBootstrap(Config config);

    [[nodiscard]] const Config& config() const noexcept;
    [[nodiscard]] JsonProjectRepository& repository() noexcept;
    [[nodiscard]] ProjectManager& manager() noexcept;

private:
    Config config_;
    JsonProjectRepository repository_;
    ProjectManager manager_;
};

}  // namespace devmanager
