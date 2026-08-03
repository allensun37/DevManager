#include "menu/MenuController.h"

#include "repository/ProjectRepository.h"

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class FailingProjectRepository final : public devmanager::ProjectRepository {
public:
    [[nodiscard]] devmanager::ProjectStore loadStore() const override {
        return {};
    }

    void saveStore(const devmanager::ProjectStore&) const override {
        throw std::runtime_error("Injected save failure");
    }
};

class ToggleFailingProjectRepository final : public devmanager::ProjectRepository {
public:
    [[nodiscard]] devmanager::ProjectStore loadStore() const override {
        return store;
    }

    void saveStore(const devmanager::ProjectStore& candidate) const override {
        if (failWrites) {
            throw std::runtime_error("Injected save failure");
        }
        store = candidate;
    }

    mutable devmanager::ProjectStore store;
    bool failWrites {false};
};

TEST(MenuControllerTest, ReportsUnknownCommandAndExitsCleanly) {
    std::istringstream input("invalid\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_NE(output.str().find("Invalid command"), std::string::npos);
    EXPECT_NE(output.str().find("Goodbye"), std::string::npos);
}

TEST(MenuControllerTest, RendersAddedProjectsAndCancelsDeletion) {
    std::istringstream input(
        "2\nDevManager\nC++, CMake\nPersonal project manager.\nIn progress\n"
        "1\n4\nmanager\n5\ncpp\n3\n1\nn\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_NE(output.str().find("DevManager"), std::string::npos);
    EXPECT_NE(output.str().find("Deletion cancelled"), std::string::npos);
}

TEST(MenuControllerTest, RejectsMalformedAndZeroProjectIds) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("Keep Me", {"CMake"}, "", "Planned"));
    std::istringstream input("3\nabc\n0\n1\nn\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_EQ(manager.listProjects().size(), 1U);
    EXPECT_NE(output.str().find("Invalid project ID"), std::string::npos);
}

TEST(MenuControllerTest, ReportsProjectValidationErrors) {
    std::istringstream input("2\n\nC++\nDescription\nIn progress\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_NE(output.str().find("Invalid project"), std::string::npos);
}

TEST(MenuControllerTest, TrimsAsciiWhitespaceFromTechnologyTags) {
    std::istringstream input(
        "2\nTrim Me\n C++, \tCMake \nDescription\nPlanned\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects()[0].techStack(),
              (std::vector<std::string>{"C++", "CMake"}));
}

TEST(MenuControllerTest, TrimsAsciiWhitespaceFromProjectIds) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("Delete Me", {"CMake"}, "Temporary project", "Planned"));
    std::istringstream input("3\n \t1 \r\ny\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_NE(output.str().find("Project deleted"), std::string::npos);
}

TEST(MenuControllerTest, DeletesProjectWhenConfirmedWithLowercaseY) {
    std::istringstream input(
        "2\nDelete Me\nCMake\nTemporary project\nPlanned\n3\n1\ny\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_NE(output.str().find("Project deleted"), std::string::npos);
}

TEST(MenuControllerTest, DeletesProjectWhenConfirmedWithUppercaseY) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("Delete Me", {"CMake"}, "Temporary project", "Planned"));
    std::istringstream input("3\n1\nY\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_NE(output.str().find("Project deleted"), std::string::npos);
}

TEST(MenuControllerTest, RejectsSignedIdAndRepromptsDeletion) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("Delete Me", {"CMake"}, "Temporary project", "Planned"));
    std::istringstream input("3\n-1\n+1\n 1 \nY\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_NE(output.str().find("Invalid project ID"), std::string::npos);
}

TEST(MenuControllerTest, AcceptsTheMaximumProjectIdAsAWellFormedId) {
    std::istringstream input("3\n18446744073709551615\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_NE(output.str().find("Project ID not found"), std::string::npos);
    EXPECT_EQ(output.str().find("Invalid project ID"), std::string::npos);
}

TEST(MenuControllerTest, RejectsOverflowingIdAndRepromptsDeletion) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("Delete Me", {"CMake"}, "Temporary project", "Planned"));
    std::istringstream input("3\n18446744073709551616\n1\nY\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_NE(output.str().find("Invalid project ID"), std::string::npos);
}

TEST(MenuControllerTest, ReportsMissingIdWithoutRequestingDeletionConfirmation) {
    std::istringstream input("3\n99\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_NE(output.str().find("Project ID not found"), std::string::npos);
    EXPECT_EQ(output.str().find("Delete this project?"), std::string::npos);
}

TEST(MenuControllerTest, EditsExistingProjectAndKeepsItsId) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("Before", {"CMake"}, "Old description", "Planned"));
    std::istringstream input("6\n1\nAfter\nC++, CMake\n\nDone\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects()[0].id(), 1U);
    EXPECT_EQ(manager.listProjects()[0].name(), "After");
    EXPECT_EQ(manager.listProjects()[0].techStack(), (std::vector<std::string>{"C++", "CMake"}));
    EXPECT_EQ(manager.listProjects()[0].description(), "");
    EXPECT_EQ(manager.listProjects()[0].status(), "Done");
    EXPECT_NE(output.str().find("Project updated"), std::string::npos);
}

TEST(MenuControllerTest, FiltersProjectsByStatusWithoutChangingStoredOrder) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("First", {"CMake"}, "", "Planned"));
    static_cast<void>(manager.addProject("Second", {"CMake"}, "", "In Progress"));
    static_cast<void>(manager.addProject("Third", {"CMake"}, "", "in progress"));
    std::istringstream input("7\n \tIN PROGRESS\r\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_EQ(output.str().find("First"), std::string::npos);
    EXPECT_NE(output.str().find("Second"), std::string::npos);
    EXPECT_NE(output.str().find("Third"), std::string::npos);
    EXPECT_EQ(manager.listProjects()[0].name(), "First");
}

TEST(MenuControllerTest, SortsProjectViewByNameWithoutChangingStoredOrder) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("Zulu", {"CMake"}, "", "Planned"));
    static_cast<void>(manager.addProject("Alpha", {"CMake"}, "", "Done"));
    std::istringstream input("8\n name \n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    const std::string rendered = output.str();
    EXPECT_LT(rendered.find("Name: Alpha"), rendered.find("Name: Zulu"));
    EXPECT_EQ(manager.listProjects()[0].name(), "Zulu");
}

TEST(MenuControllerTest, ReportsSaveFailureAndContinuesRunning) {
    FailingProjectRepository repository;
    devmanager::ProjectManager manager(repository);
    std::istringstream input("2\nWill Fail\nCMake\nDescription\nPlanned\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_NE(output.str().find("Operation failed"), std::string::npos);
    EXPECT_NE(output.str().find("Goodbye"), std::string::npos);
}

TEST(MenuControllerTest, ReportsEditSaveFailureAndKeepsTheOriginalProject) {
    ToggleFailingProjectRepository repository;
    devmanager::ProjectManager manager(repository);
    static_cast<void>(manager.addProject("Before", {"CMake"}, "Old description", "Planned"));
    repository.failWrites = true;
    std::istringstream input("6\n1\nAfter\nC++\nNew description\nDone\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects()[0].name(), "Before");
    EXPECT_NE(output.str().find("Operation failed"), std::string::npos);
    EXPECT_NE(output.str().find("Goodbye"), std::string::npos);
}

TEST(MenuControllerTest, ReportsDeleteSaveFailureAndKeepsTheOriginalProject) {
    ToggleFailingProjectRepository repository;
    devmanager::ProjectManager manager(repository);
    static_cast<void>(manager.addProject("Keep Me", {"CMake"}, "", "Planned"));
    repository.failWrites = true;
    std::istringstream input("3\n1\nY\n0\n");
    std::ostringstream output;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects()[0].name(), "Keep Me");
    EXPECT_NE(output.str().find("Operation failed"), std::string::npos);
    EXPECT_NE(output.str().find("Goodbye"), std::string::npos);
}

}  // namespace
