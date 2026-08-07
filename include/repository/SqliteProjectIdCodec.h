#pragma once

#include "common/ProjectId.h"

#include <string>
#include <string_view>

namespace devmanager {

class SqliteProjectIdCodec final {
public:
    [[nodiscard]] static std::string encode(ProjectId id);
    [[nodiscard]] static ProjectId decode(std::string_view encoded);
};

}  // namespace devmanager
