#include "repository/JsonProjectRepository.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace devmanager {

JsonProjectRepository::JsonProjectRepository(std::filesystem::path filePath)
    : filePath_(std::move(filePath)) {
}

ProjectStore JsonProjectRepository::load() const {
    if (!std::filesystem::exists(filePath_)) {
        return {};
    }

    std::ifstream input(filePath_);
    if (!input) {
        throw std::runtime_error("Unable to open project data file for reading");
    }

    try {
        nlohmann::json payload;
        input >> payload;

        ProjectStore store;
        store.nextId = payload.at("nextId").get<ProjectId>();
        for (const nlohmann::json& projectPayload : payload.at("projects")) {
            store.projects.push_back(Project::fromJson(projectPayload));
        }

        return store;
    } catch (const std::exception& error) {
        throw std::runtime_error("Invalid project data file '" + filePath_.string() +
                                 "': " + error.what());
    }
}

void JsonProjectRepository::save(const ProjectStore& store) const {
    const std::filesystem::path parentDirectory = filePath_.parent_path();
    if (!parentDirectory.empty()) {
        std::filesystem::create_directories(parentDirectory);
    }

    nlohmann::json payload;
    payload["nextId"] = store.nextId;
    payload["projects"] = nlohmann::json::array();
    for (const Project& project : store.projects) {
        payload["projects"].push_back(project.toJson());
    }

    std::ofstream output(filePath_);
    if (!output) {
        throw std::runtime_error("Unable to open project data file for writing");
    }

    output << payload.dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("Unable to write project data file");
    }
}

}  // namespace devmanager
