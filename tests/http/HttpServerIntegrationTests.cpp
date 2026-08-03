#include "application/ProjectManager.h"
#include "http/HttpServer.h"

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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ~RunningServer() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    RunningServer(const RunningServer&) = delete;
    RunningServer& operator=(const RunningServer&) = delete;

private:
    devmanager::HttpServer& server_;
    std::thread thread_;
};

httplib::Result get(devmanager::HttpServer& server, const std::string& path) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    return client.Get(path);
}

}  // namespace

TEST(HttpServerIntegrationTest, BindsDynamicPortAndStopsCleanly) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);

    server.bind();
    EXPECT_GT(server.boundPort(), 0U);
    std::thread thread([&server]() { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    server.stop();
    ASSERT_TRUE(thread.joinable());
    thread.join();
}

TEST(HttpServerIntegrationTest, EmptyProjectListReturnsJsonArray) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);

    const auto response = get(server, "/api/projects");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    EXPECT_EQ(nlohmann::json::parse(response->body), nlohmann::json::array());
}

TEST(HttpServerIntegrationTest, DeleteMissingProjectReturnsNotFoundError) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    const auto response = client.Delete("/api/projects/99");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 404);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.contains("error"));
    EXPECT_EQ(body.at("error").at("code"), "project_not_found");
}

TEST(HttpServerIntegrationTest, DeleteInvalidProjectIdReturnsBadRequest) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    const auto response = client.Delete("/api/projects/not-an-id");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 400);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.contains("error"));
    EXPECT_EQ(body.at("error").at("code"), "invalid_id");
}
