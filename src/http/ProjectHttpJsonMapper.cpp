#include "http/ProjectHttpJsonMapper.h"

#include <array>
#include <stdexcept>
#include <string_view>

namespace devmanager {
namespace {

constexpr std::array<std::string_view, 4> kInputFields{
    "name", "techStack", "description", "status"};

[[noreturn]] void throwInvalidInput(const char* message) {
    throw std::invalid_argument(message);
}

void validateInputShape(const nlohmann::json& payload) {
    if (!payload.is_object()) {
        throwInvalidInput("Project input must be a JSON object");
    }

    if (payload.size() != kInputFields.size()) {
        throwInvalidInput("Project input must contain exactly name, techStack, description, and status");
    }

    for (const auto& item : payload.items()) {
        bool known = false;
        for (const auto field : kInputFields) {
            if (item.key() == field) {
                known = true;
                break;
            }
        }
        if (!known) {
            throwInvalidInput("Project input contains an unknown field");
        }
    }

    for (const auto field : kInputFields) {
        if (!payload.contains(field)) {
            throwInvalidInput("Project input is missing a required field");
        }
    }
}

void validateInputTypes(const nlohmann::json& payload) {
    if (!payload.at("name").is_string() || !payload.at("description").is_string() ||
        !payload.at("status").is_string()) {
        throwInvalidInput("Project input text fields must be strings");
    }

    const auto& techStack = payload.at("techStack");
    if (!techStack.is_array()) {
        throwInvalidInput("Project input techStack must be an array");
    }
    for (const auto& tag : techStack) {
        if (!tag.is_string()) {
            throwInvalidInput("Project input techStack elements must be strings");
        }
    }
}

}  // namespace

nlohmann::json ProjectHttpJsonMapper::toJson(const Project& project) {
    return {
        {"id", project.id()},
        {"name", project.name()},
        {"techStack", project.techStack()},
        {"description", project.description()},
        {"status", project.status()},
    };
}

ProjectHttpInput ProjectHttpJsonMapper::parseInput(const nlohmann::json& payload) {
    try {
        validateInputShape(payload);
        validateInputTypes(payload);

        return {
            payload.at("name").get<std::string>(),
            payload.at("techStack").get<std::vector<std::string>>(),
            payload.at("description").get<std::string>(),
            payload.at("status").get<std::string>(),
        };
    } catch (const std::invalid_argument&) {
        throw;
    } catch (const nlohmann::json::exception&) {
        throw std::invalid_argument("Project input has an invalid JSON value");
    }
}

}  // namespace devmanager
