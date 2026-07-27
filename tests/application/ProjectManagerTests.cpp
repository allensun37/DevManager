#include "application/ProjectManager.h"
#include "repository/JsonProjectRepository.h"

#include <cstdlib>
#include <filesystem>
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

}  // namespace

int main() {
    devmanager::ProjectManager manager;

    const devmanager::ProjectId firstId = manager.addProject(
        "DevManager", {"C++", "CMake"}, "Personal project manager.", "开发中");
    const devmanager::ProjectId secondId = manager.addProject(
        "HTTP Server", {"C++", "Linux Socket"}, "Socket practice.", "学习中");

    expect(firstId == 1, "The first project receives ID 1");
    expect(secondId == 2, "The next project receives the next ID");

    const std::vector<devmanager::Project>& projects = manager.listProjects();
    expect(projects.size() == 2, "Manager lists every added project");
    expect(projects[0].name() == "DevManager", "Manager preserves insertion order");
    expect(projects[1].id() == secondId, "Manager stores the assigned ID");

    expect(manager.deleteProject(firstId), "Manager deletes an existing project by ID");
    expect(!manager.deleteProject(999), "Manager reports a missing project ID");
    expect(manager.listProjects().size() == 1, "Manager removes only the selected project");
    expect(manager.listProjects()[0].id() == secondId,
           "Manager retains other projects after deletion");

    const devmanager::ProjectId thirdId = manager.addProject(
        "Blog System", {"Vue"}, "Frontend practice.", "计划中");
    expect(thirdId == 3, "Manager never reuses a deleted project ID");

    const std::vector<devmanager::Project> nameResults = manager.searchByName("SERVER");
    expect(nameResults.size() == 1, "Name search supports case-insensitive partial matches");
    expect(nameResults[0].name() == "HTTP Server", "Name search returns the matching project");
    expect(manager.searchByName("").empty(), "Empty name search does not return every project");

    const std::vector<devmanager::Project> technologyResults =
        manager.searchByTechnology("cpp");
    expect(technologyResults.size() == 1,
           "Technology search normalizes C++ and cpp before matching");
    expect(technologyResults[0].name() == "HTTP Server",
           "Technology search returns the project with the matched tag");
    expect(manager.searchByTechnology("SOCKET").size() == 1,
           "Technology search supports case-insensitive partial matches");
    expect(manager.searchByTechnology("").empty(),
           "Empty technology search does not return every project");

    const std::filesystem::path persistenceDirectory =
        std::filesystem::current_path() / "project-manager-persistence-test-data";
    std::filesystem::remove_all(persistenceDirectory);
    const std::filesystem::path persistenceFile = persistenceDirectory / "projects.json";

    {
        devmanager::JsonProjectRepository repository(persistenceFile);
        devmanager::ProjectManager persistentManager(repository);
        expect(persistentManager.addProject("Persistent Project", {"C++"}, "Saved to JSON.",
                                            "开发中") == 1,
               "Persistent manager assigns the first ID");
    }

    {
        devmanager::JsonProjectRepository repository(persistenceFile);
        devmanager::ProjectManager restoredManager(repository);
        expect(restoredManager.listProjects().size() == 1,
               "Persistent manager restores projects after restart");
        expect(restoredManager.listProjects()[0].name() == "Persistent Project",
               "Persistent manager restores project fields after restart");
        expect(restoredManager.addProject("Second Project", {"CMake"}, "Next ID test.",
                                          "计划中") == 2,
               "Persistent manager restores nextId after restart");
    }

    std::filesystem::remove_all(persistenceDirectory);

    return EXIT_SUCCESS;
}
