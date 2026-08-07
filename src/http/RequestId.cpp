#include "http/RequestId.h"

#include <atomic>
#include <cstdint>

namespace devmanager::request_id {

bool isValid(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 64U) {
        return false;
    }

    for (const unsigned char character : value) {
        const bool alpha = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        if (!alpha && !digit && character != '.' && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

std::string generate() {
    static std::atomic_uint64_t nextId{1};
    return "req-" + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed));
}

std::string resolve(const std::string_view candidate, const RequestIdGenerator& generator) {
    if (isValid(candidate)) {
        return std::string(candidate);
    }

    if (generator) {
        const std::string generated = generator();
        if (isValid(generated)) {
            return generated;
        }
    }
    return generate();
}

}  // namespace devmanager::request_id
