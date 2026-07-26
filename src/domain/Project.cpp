#include "domain/Project.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace devmanager {

namespace {

bool hasNonWhitespace(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return !std::isspace(character);
    });
}

void validateProject(const std::string& name,
                     const std::vector<std::string>& techStack,
                     const std::string& status) {
    if (!hasNonWhitespace(name)) {
        throw std::invalid_argument("Project name must not be blank");
    }

    if (!hasNonWhitespace(status)) {
        throw std::invalid_argument("Project status must not be blank");
    }

    if (techStack.empty()) {
        throw std::invalid_argument("Project must have at least one technology tag");
    }

    const bool hasBlankTag = std::any_of(
        techStack.begin(), techStack.end(), [](const std::string& tag) {
            return !hasNonWhitespace(tag);
        });
    if (hasBlankTag) {
        throw std::invalid_argument("Project technology tags must not be blank");
    }
}

}  // namespace

Project::Project(ProjectId id,
                 std::string name,
                 std::vector<std::string> techStack,
                 std::string description,
                 std::string status)
    : id_(id),
      name_(std::move(name)),
      techStack_(std::move(techStack)),
      description_(std::move(description)),
      status_(std::move(status)) {
    validateProject(name_, techStack_, status_);
}

ProjectId Project::id() const noexcept {
    return id_;
}

const std::string& Project::name() const noexcept {
    return name_;
}

const std::vector<std::string>& Project::techStack() const noexcept {
    return techStack_;
}

const std::string& Project::description() const noexcept {
    return description_;
}

const std::string& Project::status() const noexcept {
    return status_;
}

nlohmann::json Project::toJson() const {
    return {
        {"id", id_},
        {"name", name_},
        {"techStack", techStack_},
        {"description", description_},
        {"status", status_},
    };
}

Project Project::fromJson(const nlohmann::json& payload) {
    return Project{
        payload.at("id").get<ProjectId>(),
        payload.at("name").get<std::string>(),
        payload.at("techStack").get<std::vector<std::string>>(),
        payload.at("description").get<std::string>(),
        payload.at("status").get<std::string>(),
    };
}

}  // namespace devmanager
