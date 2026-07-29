#include "domain/Project.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

TEST(ProjectTest, RetainsItsFields) {
    const devmanager::Project project{
        42,
        "DevManager",
        {"C++", "CMake"},
        "A command-line project manager.",
        "开发中",
    };

    EXPECT_EQ(project.id(), 42);
    EXPECT_EQ(project.name(), "DevManager");
    EXPECT_EQ(project.techStack(), (std::vector<std::string>{"C++", "CMake"}));
    EXPECT_EQ(project.description(), "A command-line project manager.");
    EXPECT_EQ(project.status(), "开发中");
}

TEST(ProjectTest, AllowsAnEmptyDescription) {
    const devmanager::Project project{43, "Notes", {"C++"}, "", "计划中"};

    EXPECT_TRUE(project.description().empty());
}

TEST(ProjectTest, RejectsInvalidRequiredFields) {
    EXPECT_THROW((devmanager::Project{1, "", {"C++"}, "description", "开发中"}),
                 std::invalid_argument);
    EXPECT_THROW((devmanager::Project{1, "Project", {"C++"}, "description", "   "}),
                 std::invalid_argument);
    EXPECT_THROW((devmanager::Project{1, "Project", {}, "description", "开发中"}),
                 std::invalid_argument);
    EXPECT_THROW((devmanager::Project{1, "Project", {"   "}, "description", "开发中"}),
                 std::invalid_argument);
}

TEST(ProjectTest, RoundTripsThroughJson) {
    const devmanager::Project project{
        42,
        "DevManager",
        {"C++", "CMake"},
        "A command-line project manager.",
        "开发中",
    };

    const auto payload = project.toJson();

    EXPECT_EQ(payload.at("id"), 42);
    EXPECT_EQ(payload.at("name"), "DevManager");
    EXPECT_EQ(payload.at("techStack"), (std::vector<std::string>{"C++", "CMake"}));
    EXPECT_EQ(payload.at("description"), "A command-line project manager.");
    EXPECT_EQ(payload.at("status"), "开发中");

    const devmanager::Project restored = devmanager::Project::fromJson(payload);
    EXPECT_EQ(restored.id(), project.id());
    EXPECT_EQ(restored.name(), project.name());
    EXPECT_EQ(restored.techStack(), project.techStack());
    EXPECT_EQ(restored.description(), project.description());
    EXPECT_EQ(restored.status(), project.status());
}

}  // namespace
