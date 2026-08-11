#include "application/ApplicationBootstrap.h"
#include "http/HttpServer.h"
#include "infrastructure/auth/EnvironmentApiKeyProvider.h"

#include <exception>
#include <iostream>

int main() {
    try {
        const devmanager::Config config =
            devmanager::ConfigLoader::load("config/devmanager.json");
        const devmanager::EnvironmentApiKeyProvider provider;
        const devmanager::ApiKeyAuthenticator authenticator(provider.load());
        devmanager::ApplicationBootstrap bootstrap(config);
        devmanager::HttpServer server(bootstrap.service(),
                                      bootstrap.logger(),
                                      authenticator,
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
