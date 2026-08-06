#include "application/ApplicationBootstrap.h"
#include "http/HttpServer.h"

#include <exception>
#include <iostream>

int main() {
    try {
        const devmanager::Config config =
            devmanager::ConfigLoader::load("config/devmanager.json");
        devmanager::ApplicationBootstrap bootstrap(config);
        devmanager::HttpServer server(bootstrap.service(),
                                      bootstrap.logger(),
                                      bootstrap.config().server.host,
                                      bootstrap.config().server.port);
        server.bind();
        server.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DevManager HTTP error: " << error.what() << '\n';
        return 1;
    }
}
