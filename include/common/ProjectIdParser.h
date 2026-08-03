#pragma once

#include "common/ProjectId.h"

#include <optional>
#include <string_view>

namespace devmanager {

[[nodiscard]] std::optional<ProjectId> parseProjectId(std::string_view input);

}  // namespace devmanager
