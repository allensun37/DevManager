#include "application/ApplicationBootstrap.h"
#include "http/HttpServer.h"

#include <exception>
#include <iostream>

int main() {
    try {
        devmanager::ApplicationBootstrap bootstrap(devmanager::Config{});
        devmanager::HttpServer server(bootstrap.service(),
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
