#pragma once

#include <string>
#include <string_view>

namespace devmanager::project_search_text {

[[nodiscard]] std::string normalizeName(std::string_view value);
[[nodiscard]] std::string normalizeStatus(std::string_view value);
[[nodiscard]] std::string normalizeTechnology(std::string_view value);
[[nodiscard]] std::string nameSortKey(std::string_view value);
[[nodiscard]] std::string statusSortKey(std::string_view value);

}  // namespace devmanager::project_search_text
