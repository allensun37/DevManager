#include "menu/MenuController.h"

#include "common/AsciiText.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> splitTechnologyTags(const std::string& input) {
    std::vector<std::string> tags;
    std::size_t start = 0;

    while (start <= input.size()) {
        const std::size_t delimiter = input.find(',', start);
        const std::size_t length = delimiter == std::string::npos ? std::string::npos
                                                                   : delimiter - start;
        tags.push_back(devmanager::ascii::trim(input.substr(start, length)));
        if (delimiter == std::string::npos) {
            return tags;
        }
        start = delimiter + 1;
    }

    return tags;
}

std::optional<devmanager::ProjectId> parseProjectId(const std::string& input) {
    const std::string trimmed = devmanager::ascii::trim(input);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    if (trimmed.front() == '+' || trimmed.front() == '-') {
        return std::nullopt;
    }

    try {
        std::size_t parsedCharacters = 0;
        const unsigned long long value = std::stoull(trimmed, &parsedCharacters);
        if (parsedCharacters != trimmed.size() || value == 0 ||
            value > std::numeric_limits<devmanager::ProjectId>::max()) {
            return std::nullopt;
        }
        return static_cast<devmanager::ProjectId>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

namespace devmanager {

MenuController::MenuController(ProjectManager& manager, std::istream& input, std::ostream& output)
    : manager_(manager), input_(input), output_(output) {
}

void MenuController::run() {
    std::string command;
    while (true) {
        printMenu();
        if (!std::getline(input_, command)) {
            output_ << "Goodbye\n";
            return;
        }

        const std::string normalizedCommand = ascii::trim(command);
        if (normalizedCommand == "0") {
            output_ << "Goodbye\n";
            return;
        }

        if (normalizedCommand == "1") {
            listProjects();
        } else if (normalizedCommand == "2") {
            addProject();
        } else if (normalizedCommand == "3") {
            deleteProject();
        } else if (normalizedCommand == "4") {
            searchByName();
        } else if (normalizedCommand == "5") {
            searchByTechnology();
        } else if (normalizedCommand == "6") {
            editProject();
        } else if (normalizedCommand == "7") {
            filterByStatus();
        } else if (normalizedCommand == "8") {
            sortProjects();
        } else {
            output_ << "Invalid command\n";
        }
    }
}

void MenuController::printMenu() const {
    output_ << "\nDevManager\n"
            << "1. List projects\n"
            << "2. Add a project\n"
            << "3. Delete a project\n"
            << "4. Search by name\n"
            << "5. Search by technology\n"
            << "6. Edit a project\n"
            << "7. Filter by status\n"
            << "8. Sort projects\n"
            << "0. Exit\n"
            << "Select: ";
}

bool MenuController::readLine(const std::string& prompt, std::string& value) const {
    output_ << prompt;
    return static_cast<bool>(std::getline(input_, value));
}

std::optional<ProjectId> MenuController::readProjectId() const {
    std::string idInput;
    while (readLine("Project ID: ", idInput)) {
        const std::optional<ProjectId> id = parseProjectId(idInput);
        if (id.has_value()) {
            return id;
        }
        output_ << "Invalid project ID\n";
    }

    output_ << "Input ended\n";
    return std::nullopt;
}

void MenuController::listProjects() const {
    printProjects(manager_.listProjects());
}

void MenuController::addProject() {
    std::string name;
    std::string technologyTags;
    std::string description;
    std::string status;
    if (!readLine("Name: ", name) || !readLine("Technology tags (comma separated): ", technologyTags) ||
        !readLine("Description: ", description) || !readLine("Status: ", status)) {
        output_ << "Input ended\n";
        return;
    }

    try {
        const ProjectId id = manager_.addProject(std::move(name), splitTechnologyTags(technologyTags),
                                                 std::move(description), std::move(status));
        output_ << "Project added with ID " << id << '\n';
    } catch (const std::invalid_argument& error) {
        output_ << "Invalid project: " << error.what() << '\n';
    } catch (const std::exception& error) {
        output_ << "Operation failed: " << error.what() << '\n';
    }
}

void MenuController::deleteProject() {
    const std::optional<ProjectId> id = readProjectId();
    if (!id.has_value()) {
        return;
    }

    const std::vector<Project> projects = manager_.listProjects();
    const auto project = std::find_if(projects.begin(), projects.end(),
                                      [id](const Project& item) {
                                          return item.id() == *id;
                                      });
    if (project == projects.end()) {
        output_ << "Project ID not found\n";
        return;
    }

    std::string confirmation;
    if (!readLine("Delete this project? (y/n): ", confirmation)) {
        output_ << "Input ended\n";
        return;
    }
    if (confirmation != "y" && confirmation != "Y") {
        output_ << "Deletion cancelled\n";
        return;
    }

    try {
        static_cast<void>(manager_.deleteProject(*id));
        output_ << "Project deleted\n";
    } catch (const std::exception& error) {
        output_ << "Operation failed: " << error.what() << '\n';
    }
}

void MenuController::editProject() {
    const std::optional<ProjectId> id = readProjectId();
    if (!id.has_value()) {
        return;
    }

    const std::vector<Project> projects = manager_.listProjects();
    const auto project = std::find_if(projects.begin(), projects.end(),
                                      [id](const Project& item) {
                                          return item.id() == *id;
                                      });
    if (project == projects.end()) {
        output_ << "Project ID not found\n";
        return;
    }

    printProjects({*project});

    std::string name;
    std::string technologyTags;
    std::string description;
    std::string status;
    if (!readLine("Name: ", name) || !readLine("Technology tags (comma separated): ", technologyTags) ||
        !readLine("Description: ", description) || !readLine("Status: ", status)) {
        output_ << "Input ended\n";
        return;
    }

    try {
        static_cast<void>(manager_.updateProject(*id, std::move(name), splitTechnologyTags(technologyTags),
                                                 std::move(description), std::move(status)));
        output_ << "Project updated\n";
    } catch (const std::invalid_argument& error) {
        output_ << "Invalid project: " << error.what() << '\n';
    } catch (const std::exception& error) {
        output_ << "Operation failed: " << error.what() << '\n';
    }
}

void MenuController::searchByName() const {
    std::string query;
    if (!readLine("Name query: ", query)) {
        output_ << "Input ended\n";
        return;
    }
    printProjects(manager_.searchByName(query));
}

void MenuController::searchByTechnology() const {
    std::string query;
    if (!readLine("Technology query: ", query)) {
        output_ << "Input ended\n";
        return;
    }
    printProjects(manager_.searchByTechnology(query));
}

void MenuController::filterByStatus() const {
    std::string status;
    if (!readLine("Status: ", status)) {
        output_ << "Input ended\n";
        return;
    }
    printProjects(manager_.filterByStatus(status));
}

void MenuController::sortProjects() const {
    std::string key;
    if (!readLine("Sort by (id/name/status): ", key)) {
        output_ << "Input ended\n";
        return;
    }

    const std::string normalizedKey = ascii::toLower(ascii::trim(key));
    if (normalizedKey == "id") {
        printProjects(manager_.sortedProjects(ProjectSortKey::Id));
    } else if (normalizedKey == "name") {
        printProjects(manager_.sortedProjects(ProjectSortKey::Name));
    } else if (normalizedKey == "status") {
        printProjects(manager_.sortedProjects(ProjectSortKey::Status));
    } else {
        output_ << "Invalid sort key\n";
    }
}

void MenuController::printProjects(const std::vector<Project>& projects) const {
    if (projects.empty()) {
        output_ << "No projects found\n";
        return;
    }

    for (const Project& project : projects) {
        output_ << "ID: " << project.id() << '\n'
                << "Name: " << project.name() << '\n'
                << "Technology: ";
        for (std::size_t index = 0; index < project.techStack().size(); ++index) {
            if (index != 0) {
                output_ << ", ";
            }
            output_ << project.techStack()[index];
        }
        output_ << '\n'
                << "Description: " << project.description() << '\n'
                << "Status: " << project.status() << '\n';
    }
}

}  // namespace devmanager
