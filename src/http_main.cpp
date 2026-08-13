#include "application/ApplicationBootstrap.h"
#include "application/ReadinessState.h"
#include "application/ServiceLifecycle.h"
#include "http/HttpServer.h"
#include "infrastructure/auth/EnvironmentApiKeyProvider.h"
#include "infrastructure/runtime/SignalHandler.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

int main() {
    try {
        const devmanager::Config config =
            devmanager::ConfigLoader::load("config/devmanager.json");
        const devmanager::EnvironmentApiKeyProvider provider;
        const devmanager::ApiKeyAuthenticator authenticator(provider.load());
        devmanager::ApplicationBootstrap bootstrap(config);
        devmanager::ReadinessState readiness;
        devmanager::SignalHandler signals;
        signals.install();
        devmanager::HttpServer server(bootstrap.service(),
                                      bootstrap.logger(),
                                      authenticator,
                                      readiness,
                                      bootstrap.config().server.host,
                                      bootstrap.config().server.port);
        devmanager::ServiceLifecycle lifecycle(
            readiness, signals, server, bootstrap.logger());
        server.bind();
        server.runAsync();
        if (!server.waitUntilListening(std::chrono::seconds(2))) {
            bootstrap.logger().error("HTTP listener failed before readiness");
            static_cast<void>(lifecycle.markFailed());
            return 1;
        }
        lifecycle.markReady();

        while (!signals.stopRequested()) {
            if (!server.isListening()) {
                bootstrap.logger().error("HTTP listener stopped unexpectedly");
                static_cast<void>(lifecycle.markFailed());
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        const devmanager::ServiceExit exit = lifecycle.stopWhenRequested();
        return exit == devmanager::ServiceExit::Stopped ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "DevManager HTTP error: " << error.what() << '\n';
        return 1;
    }
}
