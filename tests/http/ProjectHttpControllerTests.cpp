#include "application/ProjectManager.h"
#include "http/HttpServer.h"
#include "http/ProjectHttpController.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace {

class RunningServer final {
public:
    explicit RunningServer(devmanager::HttpServer& server) : server_(server) {
        server_.bind();
        thread_ = std::thread([this]() { server_.run(); });
    }

    ~RunningServer() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    RunningServer(const RunningServer&) = delete;
    RunningServer& operator=(const RunningServer&) = delete;

    bool waitUntilReady() const {
        httplib::Client client("127.0.0.1", static_cast<int>(server_.boundPort()));
        client.set_connection_timeout(0, 100000);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            if (client.Get("/api/projects")) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

private:
    devmanager::HttpServer& server_;
    std::thread thread_;
};

httplib::Result get(devmanager::HttpServer& server, const std::string& path) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_connection_timeout(0, 100000);
    return client.Get(path);
}

void expectJsonContentType(const httplib::Response& response) {
    EXPECT_EQ(response.get_header_value("Content-Type"),
              "application/json; charset=UTF-8");
}

}  // namespace

TEST(ProjectHttpControllerTest, RegistersProjectRoutesWithoutThrowing) {
    devmanager::ProjectManager manager;
    httplib::Server server;
    devmanager::ProjectHttpController controller(manager);

    EXPECT_NO_THROW(controller.registerRoutes(server));
}

TEST(ProjectHttpControllerTest, ListsAllProjectsWhenNoQueryIsPresent) {
    devmanager::ProjectManager manager;
    ASSERT_EQ(manager.addProject("Zeta", {"C++"}, "last", "active"), 1U);
    ASSERT_EQ(manager.addProject("Alpha", {"CMake"}, "first", "planned"), 2U);
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.is_array());
    ASSERT_EQ(body.size(), 2U);
    EXPECT_EQ(body.at(0).at("name"), "Zeta");
    EXPECT_EQ(body.at(1).at("name"), "Alpha");
}

TEST(ProjectHttpControllerTest, AllowsSortWithoutAFilter) {
    devmanager::ProjectManager manager;
    ASSERT_EQ(manager.addProject("Zeta", {"C++"}, "", "active"), 1U);
    ASSERT_EQ(manager.addProject("Alpha", {"CMake"}, "", "planned"), 2U);
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?sort=name");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.is_array());
    ASSERT_EQ(body.size(), 2U);
    EXPECT_EQ(body.at(0).at("name"), "Alpha");
    EXPECT_EQ(body.at(1).at("name"), "Zeta");
}

TEST(ProjectHttpControllerTest, SearchesByNameOrTechnology) {
    devmanager::ProjectManager manager;
    ASSERT_EQ(manager.addProject("DevManager", {"C++", "CMake"}, "", "active"), 1U);
    ASSERT_EQ(manager.addProject("Website", {"React"}, "", "planned"), 2U);
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto nameResponse = get(server, "/api/projects?name=dev");
    ASSERT_TRUE(nameResponse);
    ASSERT_EQ(nameResponse->status, 200);
    expectJsonContentType(*nameResponse);
    const auto nameBody = nlohmann::json::parse(nameResponse->body);
    ASSERT_TRUE(nameBody.is_array());
    ASSERT_EQ(nameBody.size(), 1U);
    EXPECT_EQ(nameBody.at(0).at("name"), "DevManager");

    const auto technologyResponse = get(server, "/api/projects?technology=cpp");
    ASSERT_TRUE(technologyResponse);
    ASSERT_EQ(technologyResponse->status, 200);
    expectJsonContentType(*technologyResponse);
    const auto technologyBody = nlohmann::json::parse(technologyResponse->body);
    ASSERT_TRUE(technologyBody.is_array());
    ASSERT_EQ(technologyBody.size(), 1U);
    EXPECT_EQ(technologyBody.at(0).at("name"), "DevManager");
}

TEST(ProjectHttpControllerTest, FiltersByStatus) {
    devmanager::ProjectManager manager;
    ASSERT_EQ(manager.addProject("One", {"C++"}, "", "active"), 1U);
    ASSERT_EQ(manager.addProject("Two", {"CMake"}, "", "planned"), 2U);
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?status=%20ACTIVE%20");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.is_array());
    ASSERT_EQ(body.size(), 1U);
    EXPECT_EQ(body.at(0).at("name"), "One");
}

TEST(ProjectHttpControllerTest, RejectsMultipleFilterParameters) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?name=dev&status=active");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 400);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_EQ(body.at("error").at("code"), "invalid_query");
}

TEST(ProjectHttpControllerTest, RejectsUnknownAndRepeatedQueryParameters) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto unknownResponse = get(server, "/api/projects?unknown=value");
    ASSERT_TRUE(unknownResponse);
    ASSERT_EQ(unknownResponse->status, 400);
    expectJsonContentType(*unknownResponse);
    EXPECT_EQ(nlohmann::json::parse(unknownResponse->body).at("error").at("code"),
              "invalid_query");

    const auto repeatedResponse = get(server, "/api/projects?name=one&name=two");
    ASSERT_TRUE(repeatedResponse);
    ASSERT_EQ(repeatedResponse->status, 400);
    expectJsonContentType(*repeatedResponse);
    EXPECT_EQ(nlohmann::json::parse(repeatedResponse->body).at("error").at("code"),
              "invalid_query");
}

TEST(ProjectHttpControllerTest, RejectsInvalidSortKey) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?sort=priority");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 400);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_EQ(body.at("error").at("code"), "invalid_query");
}
