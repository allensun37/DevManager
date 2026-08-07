#include "common/ProjectSearchText.h"

#include "common/AsciiText.h"

namespace {

bool isAsciiLetterOrDigit(unsigned char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9');
}

}  // namespace

namespace devmanager::project_search_text {

std::string normalizeName(std::string_view value) {
    return ascii::toLower(value);
}

std::string normalizeStatus(std::string_view value) {
    return ascii::toLower(ascii::trim(value));
}

std::string normalizeTechnology(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character == '+' && index + 1 < value.size() && value[index + 1] == '+') {
            normalized += "pp";
            ++index;
            continue;
        }

        if (character < 128 && !isAsciiLetterOrDigit(character)) {
            continue;
        }

        normalized.push_back(static_cast<char>(character));
    }

    return ascii::toLower(normalized);
}

std::string nameSortKey(std::string_view value) {
    return ascii::toLower(value);
}

std::string statusSortKey(std::string_view value) {
    return ascii::toLower(value);
}

}  // namespace devmanager::project_search_text
