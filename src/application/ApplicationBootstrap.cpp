#include "application/ApplicationBootstrap.h"

#include "application/ProjectManager.h"

#include <utility>

namespace devmanager {

ApplicationBootstrap::ApplicationBootstrap(Config config)
    : config_(std::move(config)),
      logger_(config_.logging.path, config_.logging.level),
      repository_(config_.storage.path),
      manager_(repository_),
      service_(manager_) {}

const Config& ApplicationBootstrap::config() const noexcept {
    return config_;
}

JsonProjectRepository& ApplicationBootstrap::repository() noexcept {
    return repository_;
}

ProjectManager& ApplicationBootstrap::manager() noexcept {
    return manager_;
}

Logger& ApplicationBootstrap::logger() noexcept {
    return logger_;
}

ProjectService& ApplicationBootstrap::service() noexcept {
    return service_;
}

}  // namespace devmanager
