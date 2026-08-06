#include "application/ApplicationBootstrap.h"
#include "menu/MenuController.h"

#include <exception>
#include <iostream>

int main() {
    try {
        const devmanager::Config config =
            devmanager::ConfigLoader::load("config/devmanager.json");
        devmanager::ApplicationBootstrap bootstrap(config);
        devmanager::MenuController controller(bootstrap.manager(), std::cin, std::cout);
        controller.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DevManager error: " << error.what() << '\n';
        return 1;
    }
}
