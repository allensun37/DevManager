#include "application/ProjectManager.h"
#include "http/HttpServer.h"
#include "http/ProjectHttpController.h"
#include "repository/ProjectRepository.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

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
    client.set_path_encode(false);
    return client.Get(path);
}

httplib::Result postJson(devmanager::HttpServer& server,
                         const std::string& body) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_connection_timeout(0, 100000);
    return client.Post("/api/projects", body, "application/json");
}

httplib::Result putJson(devmanager::HttpServer& server,
                        const std::string& path,
                        const std::string& body) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_connection_timeout(0, 100000);
    return client.Put(path, body, "application/json");
}

class FakeProjectRepository final : public devmanager::ProjectRepository {
public:
    explicit FakeProjectRepository(devmanager::ProjectStore initialStore = {})
        : store_(std::move(initialStore)) {}

    [[nodiscard]] devmanager::ProjectStore loadStore() const override {
        return store_;
    }

    void saveStore(const devmanager::ProjectStore& candidate) const override {
        if (failSaves_) {
            throw std::runtime_error("injected save failure");
        }
        store_ = candidate;
        savedStores_.push_back(candidate);
    }

    void setFailSaves(bool fail) const noexcept {
        failSaves_ = fail;
    }

    [[nodiscard]] const std::vector<devmanager::ProjectStore>& savedStores() const {
        return savedStores_;
    }

private:
    mutable devmanager::ProjectStore store_;
    mutable std::vector<devmanager::ProjectStore> savedStores_;
    mutable bool failSaves_ {false};
};

std::string validProjectJson(const std::string& name = "DevManager") {
    return nlohmann::json{
        {"name", name},
        {"techStack", {"C++", "CMake"}},
        {"description", "project"},
        {"status", "active"},
    }
        .dump();
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

    const auto emptyKeyResponse = get(server, "/api/projects?=value");
    ASSERT_TRUE(emptyKeyResponse);
    ASSERT_EQ(emptyKeyResponse->status, 400);
    expectJsonContentType(*emptyKeyResponse);
    EXPECT_EQ(nlohmann::json::parse(emptyKeyResponse->body).at("error").at("code"),
              "invalid_query");

    const auto repeatedResponse = get(server, "/api/projects?name=one&name=two");
    ASSERT_TRUE(repeatedResponse);
    ASSERT_EQ(repeatedResponse->status, 400);
    expectJsonContentType(*repeatedResponse);
    EXPECT_EQ(nlohmann::json::parse(repeatedResponse->body).at("error").at("code"),
              "invalid_query");

    const auto repeatedSameNameResponse = get(server, "/api/projects?name=one&name=one");
    ASSERT_TRUE(repeatedSameNameResponse);
    ASSERT_EQ(repeatedSameNameResponse->status, 400);
    expectJsonContentType(*repeatedSameNameResponse);
    EXPECT_EQ(nlohmann::json::parse(repeatedSameNameResponse->body)
                  .at("error")
                  .at("code"),
              "invalid_query");

    const auto repeatedSortResponse = get(server, "/api/projects?sort=name&sort=name");
    ASSERT_TRUE(repeatedSortResponse);
    ASSERT_EQ(repeatedSortResponse->status, 400);
    expectJsonContentType(*repeatedSortResponse);
    EXPECT_EQ(nlohmann::json::parse(repeatedSortResponse->body).at("error").at("code"),
              "invalid_query");

    const auto emptyValueResponse = get(server, "/api/projects?name=");
    ASSERT_TRUE(emptyValueResponse);
    ASSERT_EQ(emptyValueResponse->status, 400);
    expectJsonContentType(*emptyValueResponse);
    EXPECT_EQ(nlohmann::json::parse(emptyValueResponse->body).at("error").at("code"),
              "invalid_query");

    const auto encodedDelimiterResponse =
        get(server, "/api/projects?name=one%26name=one");
    ASSERT_TRUE(encodedDelimiterResponse);
    ASSERT_EQ(encodedDelimiterResponse->status, 200);
    expectJsonContentType(*encodedDelimiterResponse);
    EXPECT_TRUE(nlohmann::json::parse(encodedDelimiterResponse->body).is_array());
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

TEST(ProjectHttpControllerTest, CreatesProjectAndReturns201) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = postJson(server, validProjectJson());

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 201);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("id"), 1U);
    EXPECT_EQ(body.at("name"), "DevManager");
    EXPECT_EQ(body.at("techStack"), nlohmann::json({"C++", "CMake"}));
    EXPECT_EQ(body.at("description"), "project");
    EXPECT_EQ(body.at("status"), "active");
}

TEST(ProjectHttpControllerTest, RejectsMalformedJsonAndInvalidFields) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto malformed = postJson(server, "{not-json");
    ASSERT_TRUE(malformed);
    ASSERT_EQ(malformed->status, 400);
    EXPECT_EQ(nlohmann::json::parse(malformed->body).at("error").at("code"),
              "invalid_json");

    const auto unknown = postJson(
        server,
        R"({"name":"DevManager","techStack":["C++"],"description":"project","status":"active","id":9})");
    ASSERT_TRUE(unknown);
    ASSERT_EQ(unknown->status, 400);
    EXPECT_EQ(nlohmann::json::parse(unknown->body).at("error").at("code"),
              "invalid_request");

    const auto invalidType = postJson(
        server,
        R"({"name":"DevManager","techStack":"C++","description":"project","status":"active"})");
    ASSERT_TRUE(invalidType);
    ASSERT_EQ(invalidType->status, 400);
    EXPECT_EQ(nlohmann::json::parse(invalidType->body).at("error").at("code"),
              "invalid_request");
}

TEST(ProjectHttpControllerTest, UpdatesAllEditableFieldsAndPreservesId) {
    devmanager::ProjectManager manager;
    ASSERT_EQ(manager.addProject("Before", {"C++"}, "old", "planned"), 1U);
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = putJson(
        server,
        "/api/projects/1",
        R"({"name":"After","techStack":["CMake","Ninja"],"description":"new","status":"active"})");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("id"), 1U);
    EXPECT_EQ(body.at("name"), "After");
    EXPECT_EQ(body.at("techStack"), nlohmann::json({"CMake", "Ninja"}));
    EXPECT_EQ(body.at("description"), "new");
    EXPECT_EQ(body.at("status"), "active");
}

TEST(ProjectHttpControllerTest, UpdatesMissingProjectAndReturns404) {
    devmanager::ProjectManager manager;
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = putJson(
        server,
        "/api/projects/99",
        R"({"name":"After","techStack":["CMake"],"description":"new","status":"active"})");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 404);
    expectJsonContentType(*response);
    EXPECT_EQ(nlohmann::json::parse(response->body).at("error").at("code"),
              "project_not_found");
}

TEST(ProjectHttpControllerTest, DeletesProjectAndReturns204) {
    devmanager::ProjectManager manager;
    ASSERT_EQ(manager.addProject("DevManager", {"C++"}, "project", "active"), 1U);
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    const auto response = client.Delete("/api/projects/1");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 204);
    EXPECT_TRUE(response->body.empty());
    EXPECT_TRUE(manager.listProjects().empty());
}

TEST(ProjectHttpControllerTest, MapsSaveFailureToPersistenceFailureAndRollsBack) {
    FakeProjectRepository repository;
    repository.setFailSaves(true);
    devmanager::ProjectManager manager(repository);
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto failed = postJson(server, validProjectJson());

    ASSERT_TRUE(failed);
    ASSERT_EQ(failed->status, 500);
    const auto failureBody = nlohmann::json::parse(failed->body);
    EXPECT_EQ(failureBody.at("error").at("code"), "persistence_failure");
    EXPECT_EQ(failureBody.at("error").at("message"), "Persistence operation failed");
    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_TRUE(repository.savedStores().empty());

    repository.setFailSaves(false);
    const auto retry = postJson(server, validProjectJson("Retry"));
    ASSERT_TRUE(retry);
    ASSERT_EQ(retry->status, 201);
    EXPECT_EQ(nlohmann::json::parse(retry->body).at("id"), 1U);
}

TEST(ProjectHttpControllerTest, MapsIdExhaustionToConflict) {
    constexpr devmanager::ProjectId penultimateId =
        std::numeric_limits<devmanager::ProjectId>::max() - 1U;
    FakeProjectRepository repository(devmanager::ProjectStore{
        std::numeric_limits<devmanager::ProjectId>::max(),
        {devmanager::Project{penultimateId, "Existing", {"C++"}, "", "active"}}});
    devmanager::ProjectManager manager(repository);
    devmanager::HttpServer server(manager, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = postJson(server, validProjectJson());

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 409);
    EXPECT_EQ(nlohmann::json::parse(response->body).at("error").at("code"),
              "id_exhausted");
    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects().front().id(), penultimateId);
}
