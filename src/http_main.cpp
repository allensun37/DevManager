#include "application/ProjectManager.h"
#include "http/HttpServer.h"
#include "repository/JsonProjectRepository.h"

#include <exception>
#include <iostream>

int main() {
    try {
        devmanager::JsonProjectRepository repository("data/projects.json");
        devmanager::ProjectManager manager(repository);
        devmanager::HttpServer server(manager, "127.0.0.1", 8080);
        server.bind();
        server.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DevManager HTTP error: " << error.what() << '\n';
        return 1;
    }
}
