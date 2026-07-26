#include "domain/Project.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <typename Operation>
void expectInvalidArgument(Operation operation, const std::string& message) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }

    std::cerr << "Test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

}  // namespace

int main() {
    const devmanager::Project project{
        42,
        "DevManager",
        {"C++", "CMake"},
        "A command-line project manager.",
        "开发中",
    };

    expect(project.id() == 42, "Project keeps its permanent ID");
    expect(project.name() == "DevManager", "Project keeps its name");
    expect(project.techStack() == std::vector<std::string>{"C++", "CMake"},
           "Project keeps technology tags");
    expect(project.description() == "A command-line project manager.",
           "Project keeps its description");
    expect(project.status() == "开发中", "Project keeps its status");

    const devmanager::Project emptyDescription{
        43,
        "Notes",
        {"C++"},
        "",
        "计划中",
    };
    expect(emptyDescription.description().empty(),
           "Project allows an empty description");

    expectInvalidArgument(
        [] {
            devmanager::Project{1, "", {"C++"}, "description", "开发中"};
        },
        "Project rejects an empty name");
    expectInvalidArgument(
        [] {
            devmanager::Project{1, "Project", {"C++"}, "description", "   "};
        },
        "Project rejects a blank status");
    expectInvalidArgument(
        [] {
            devmanager::Project{1, "Project", {}, "description", "开发中"};
        },
        "Project requires at least one technology tag");
    expectInvalidArgument(
        [] {
            devmanager::Project{1, "Project", {"   "}, "description", "开发中"};
        },
        "Project rejects blank technology tags");

    const auto payload = project.toJson();
    expect(payload.at("id") == 42, "Project JSON keeps its ID");
    expect(payload.at("name") == "DevManager", "Project JSON keeps its name");
    expect(payload.at("techStack") == std::vector<std::string>{"C++", "CMake"},
           "Project JSON keeps technology tags");
    expect(payload.at("description") == "A command-line project manager.",
           "Project JSON keeps its description");
    expect(payload.at("status") == "开发中", "Project JSON keeps its status");

    const devmanager::Project restored = devmanager::Project::fromJson(payload);
    expect(restored.id() == project.id(), "Project restores its ID from JSON");
    expect(restored.name() == project.name(), "Project restores its name from JSON");
    expect(restored.techStack() == project.techStack(),
           "Project restores technology tags from JSON");
    expect(restored.description() == project.description(),
           "Project restores its description from JSON");
    expect(restored.status() == project.status(), "Project restores its status from JSON");

    return EXIT_SUCCESS;
}
