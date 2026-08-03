#pragma once

#include <string>
#include <string_view>

namespace devmanager::ascii {

[[nodiscard]] std::string trim(std::string_view value);
[[nodiscard]] std::string toLower(std::string_view value);

}  // namespace devmanager::ascii
