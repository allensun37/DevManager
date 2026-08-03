#include "application/ProjectManager.h"
#include "http/ProjectHttpController.h"

#include <gtest/gtest.h>

TEST(ProjectHttpControllerTest, RegistersProjectRoutesWithoutThrowing) {
    devmanager::ProjectManager manager;
    httplib::Server server;
    devmanager::ProjectHttpController controller(manager);

    EXPECT_NO_THROW(controller.registerRoutes(server));
}
