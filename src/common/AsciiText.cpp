#include "common/AsciiText.h"

namespace {

bool isAsciiWhitespace(unsigned char character) {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\f' || character == '\v';
}

}  // namespace

namespace devmanager::ascii {

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && isAsciiWhitespace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first && isAsciiWhitespace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return std::string(value.substr(first, last - first));
}

std::string toLower(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());

    for (const unsigned char character : value) {
        if (character >= 'A' && character <= 'Z') {
            normalized.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            normalized.push_back(static_cast<char>(character));
        }
    }

    return normalized;
}

}  // namespace devmanager::ascii
