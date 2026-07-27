#include "menu/MenuController.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

int main() {
    std::istringstream input("invalid\n0\n");
    std::ostringstream output;
    devmanager::ProjectManager manager;
    devmanager::MenuController controller(manager, input, output);

    controller.run();

    expect(output.str().find("Invalid command") != std::string::npos,
           "Menu reports an unknown command");
    expect(output.str().find("Goodbye") != std::string::npos,
           "Menu exits after command 0");

    std::istringstream interactionInput(
        "2\nDevManager\nC++, CMake\nPersonal project manager.\nIn progress\n"
        "1\n4\nmanager\n5\ncpp\n3\n1\nn\n0\n");
    std::ostringstream interactionOutput;
    devmanager::ProjectManager interactionManager;
    devmanager::MenuController interactionController(interactionManager, interactionInput,
                                                      interactionOutput);

    interactionController.run();

    expect(interactionManager.listProjects().size() == 1,
           "Cancelled delete keeps the project");
    expect(interactionOutput.str().find("DevManager") != std::string::npos,
           "List and searches render the added project");
    expect(interactionOutput.str().find("Deletion cancelled") != std::string::npos,
           "Delete requires y confirmation");

    std::istringstream recoveryInput("3\nabc\n3\n0\n0\n");
    std::ostringstream recoveryOutput;
    devmanager::ProjectManager recoveryManager;
    devmanager::MenuController recoveryController(recoveryManager, recoveryInput, recoveryOutput);

    recoveryController.run();

    expect(recoveryOutput.str().find("Invalid project ID") != std::string::npos,
           "Malformed and zero IDs are rejected without terminating the menu");

    std::istringstream validationInput("2\n\nC++\nDescription\nIn progress\n0\n");
    std::ostringstream validationOutput;
    devmanager::ProjectManager validationManager;
    devmanager::MenuController validationController(validationManager, validationInput,
                                                     validationOutput);

    validationController.run();

    expect(validationManager.listProjects().empty(), "Blank project names are not added");
    expect(validationOutput.str().find("Invalid project") != std::string::npos,
           "Project validation errors are displayed to the user");

    std::istringstream deletionInput(
        "2\nDelete Me\nCMake\nTemporary project\nPlanned\n3\n1\ny\n0\n");
    std::ostringstream deletionOutput;
    devmanager::ProjectManager deletionManager;
    devmanager::MenuController deletionController(deletionManager, deletionInput, deletionOutput);

    deletionController.run();

    expect(deletionManager.listProjects().empty(), "y confirmation deletes the selected project");
    expect(deletionOutput.str().find("Project deleted") != std::string::npos,
           "Successful deletion is reported to the user");

    return EXIT_SUCCESS;
}
