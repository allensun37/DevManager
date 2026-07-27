#include "repository/JsonProjectRepository.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::filesystem::path testDirectory() {
    return std::filesystem::current_path() / "repository-test-data";
}

}  // namespace

int main() {
    const std::filesystem::path directory = testDirectory();
    std::filesystem::remove_all(directory);
    const std::filesystem::path filePath = directory / "projects.json";

    devmanager::JsonProjectRepository repository(filePath);
    const devmanager::ProjectStore emptyStore = repository.load();
    expect(emptyStore.nextId == 1, "Missing data file starts with ID 1");
    expect(emptyStore.projects.empty(), "Missing data file starts with no projects");

    const devmanager::ProjectStore expected {
        3,
        {devmanager::Project {1, "DevManager", {"C++", "CMake"},
                             "Personal project manager.", "开发中"},
         devmanager::Project {2, "HTTP Server", {"C++", "Linux Socket"},
                             "Socket practice.", "学习中"}},
    };

    repository.save(expected);
    expect(std::filesystem::exists(filePath), "Repository creates its JSON data file");

    const devmanager::ProjectStore restored = repository.load();
    expect(restored.nextId == 3, "Repository restores nextId");
    expect(restored.projects.size() == 2, "Repository restores every saved project");
    expect(restored.projects[0].name() == "DevManager", "Repository preserves project data");
    expect(restored.projects[1].techStack()[1] == "Linux Socket",
           "Repository preserves technology tags");

    const std::string corruptContent = "{ this is not valid JSON";
    {
        std::ofstream corruptFile(filePath);
        corruptFile << corruptContent;
    }

    std::string errorMessage;
    try {
        static_cast<void>(repository.load());
    } catch (const std::exception& error) {
        errorMessage = error.what();
    }
    expect(errorMessage.find("Invalid project data file") != std::string::npos,
           "Corrupt data reports a clear repository error");

    std::string unchangedContent;
    {
        std::ifstream unchangedFile(filePath);
        unchangedContent.assign(std::istreambuf_iterator<char>(unchangedFile),
                                std::istreambuf_iterator<char>());
    }
    expect(unchangedContent == corruptContent,
           "A failed load does not overwrite corrupt project data");

    std::filesystem::remove_all(directory);
    return EXIT_SUCCESS;
}
