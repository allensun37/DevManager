#include "application/ApplicationBootstrap.h"

#include "application/ProjectManager.h"

#include <utility>

namespace devmanager {

ApplicationBootstrap::ApplicationBootstrap(Config config)
    : config_(std::move(config)), repository_(config_.storage.path), manager_(repository_) {}

const Config& ApplicationBootstrap::config() const noexcept {
    return config_;
}

JsonProjectRepository& ApplicationBootstrap::repository() noexcept {
    return repository_;
}

ProjectManager& ApplicationBootstrap::manager() noexcept {
    return manager_;
}

}  // namespace devmanager
