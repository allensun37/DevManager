#include "application/ProjectManager.h"
#include "menu/MenuController.h"
#include "repository/JsonProjectRepository.h"

#include <exception>
#include <iostream>

int main() {
    try {
        devmanager::JsonProjectRepository repository("data/projects.json");
        devmanager::ProjectManager manager(repository);
        devmanager::MenuController controller(manager, std::cin, std::cout);
        controller.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DevManager error: " << error.what() << '\n';
        return 1;
    }
}
