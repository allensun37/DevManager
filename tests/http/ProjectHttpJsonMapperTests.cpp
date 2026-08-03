#include "domain/Project.h"
#include "http/HttpError.h"
#include "http/ProjectHttpJsonMapper.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace devmanager {
namespace {

TEST(ProjectHttpJsonMapperTest, ProjectJsonHasExactResponseShape) {
    const Project project{42,
                          "DevManager",
                          {"C++", "CMake"},
                          "Project manager",
                          "active"};

    const nlohmann::json expected{{"id", 42},
                                  {"name", "DevManager"},
                                  {"techStack", {"C++", "CMake"}},
                                  {"description", "Project manager"},
                                  {"status", "active"}};

    EXPECT_EQ(ProjectHttpJsonMapper::toJson(project), expected);
}

TEST(ProjectHttpJsonMapperTest, ParsesAllInputFields) {
    const auto input = ProjectHttpJsonMapper::parseInput(
        nlohmann::json{{"name", "DevManager"},
                       {"techStack", {"C++", "CMake"}},
                       {"description", "Project manager"},
                       {"status", "active"}});

    EXPECT_EQ(input.name, "DevManager");
    EXPECT_EQ(input.techStack, (std::vector<std::string>{"C++", "CMake"}));
    EXPECT_EQ(input.description, "Project manager");
    EXPECT_EQ(input.status, "active");
}

TEST(ProjectHttpJsonMapperTest, RejectsMissingFields) {
    const nlohmann::json payload{{"name", "DevManager"},
                                 {"techStack", {"C++"}},
                                 {"description", "Project manager"}};

    EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(payload)),
                 std::invalid_argument);
}

TEST(ProjectHttpJsonMapperTest, RejectsIdField) {
    const nlohmann::json payload{{"id", 1},
                                 {"name", "DevManager"},
                                 {"techStack", {"C++"}},
                                 {"description", "Project manager"},
                                 {"status", "active"}};

    EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(payload)),
                 std::invalid_argument);
}

TEST(ProjectHttpJsonMapperTest, RejectsUnknownField) {
    const nlohmann::json payload{{"name", "DevManager"},
                                 {"techStack", {"C++"}},
                                 {"description", "Project manager"},
                                 {"status", "active"},
                                 {"extra", true}};

    EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(payload)),
                 std::invalid_argument);
}

TEST(ProjectHttpJsonMapperTest, RejectsNullAndNonObjectPayloads) {
    EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(nlohmann::json{})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(nlohmann::json::object())),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(nullptr)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(nlohmann::json::array())),
                 std::invalid_argument);
}

TEST(ProjectHttpJsonMapperTest, RejectsWrongFieldTypes) {
    const nlohmann::json base{{"name", "DevManager"},
                              {"techStack", {"C++"}},
                              {"description", "Project manager"},
                              {"status", "active"}};

    for (const auto& [field, value] : std::vector<std::pair<std::string, nlohmann::json>>{
             {"name", 42},
             {"techStack", "C++"},
             {"description", 42},
             {"status", 42},
         }) {
        auto payload = base;
        payload[field] = value;
        EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(payload)),
                     std::invalid_argument)
            << "field: " << field;
    }
}

TEST(ProjectHttpJsonMapperTest, RejectsNonStringTechnologyElements) {
    const nlohmann::json payload{{"name", "DevManager"},
                                 {"techStack", {"C++", 42}},
                                 {"description", "Project manager"},
                                 {"status", "active"}};

    EXPECT_THROW(static_cast<void>(ProjectHttpJsonMapper::parseInput(payload)),
                 std::invalid_argument);
}

TEST(HttpErrorTest, ProducesStableErrorEnvelope) {
    const HttpError error{400, "invalid_query", "Invalid query"};

    const nlohmann::json expected{{"error", {{"code", "invalid_query"},
                                              {"message", "Invalid query"}}}};

    EXPECT_EQ(error.toJson(), expected);
    EXPECT_FALSE(error.toJson().contains("status"));
}

}  // namespace
}  // namespace devmanager
