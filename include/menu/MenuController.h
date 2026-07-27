#pragma once

#include "application/ProjectManager.h"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace devmanager {

class MenuController {
public:
    MenuController(ProjectManager& manager, std::istream& input, std::ostream& output);

    void run();

private:
    void printMenu() const;
    [[nodiscard]] bool readLine(const std::string& prompt, std::string& value) const;
    void listProjects() const;
    void addProject();
    void deleteProject();
    void searchByName() const;
    void searchByTechnology() const;
    void printProjects(const std::vector<Project>& projects) const;

    ProjectManager& manager_;
    std::istream& input_;
    std::ostream& output_;
};

}  // namespace devmanager
