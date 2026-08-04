#include "common/ProjectIdParser.h"

#include "common/AsciiText.h"

#include <charconv>
#include <optional>
#include <string>
#include <system_error>

namespace devmanager {

std::optional<ProjectId> parseProjectId(std::string_view input) {
    const std::string trimmed = ascii::trim(input);
    if (trimmed.empty() || trimmed.front() == '+' || trimmed.front() == '-') {
        return std::nullopt;
    }

    ProjectId value = 0;
    const auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value, 10);
    if (result.ec != std::errc{} || result.ptr != trimmed.data() + trimmed.size() || value == 0) {
        return std::nullopt;
    }

    return value;
}

}  // namespace devmanager
