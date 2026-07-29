#include "menu/MenuController.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace {

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
    std::istringstream input("3\nabc\n3\n0\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

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

}  // namespace
