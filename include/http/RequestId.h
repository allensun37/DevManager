#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace devmanager {

using RequestIdGenerator = std::function<std::string()>;

namespace request_id {

[[nodiscard]] bool isValid(std::string_view value) noexcept;
[[nodiscard]] std::string generate();
[[nodiscard]] std::string resolve(std::string_view candidate,
                                  const RequestIdGenerator& generator);

}  // namespace request_id

}  // namespace devmanager
