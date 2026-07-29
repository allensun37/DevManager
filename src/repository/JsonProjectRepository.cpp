#include "repository/JsonProjectRepository.h"

#include "repository/FileReplacer.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {

void validateProjectStore(const devmanager::ProjectStore& store) {
    if (store.projects.empty()) {
        if (store.nextId == 0) {
            throw std::invalid_argument("nextId must be greater than zero");
        }
        return;
    }

    std::unordered_set<devmanager::ProjectId> projectIds;
    devmanager::ProjectId maximumId = 0;
    for (const devmanager::Project& project : store.projects) {
        const devmanager::ProjectId id = project.id();
        if (id == 0) {
            throw std::invalid_argument("Project IDs must be greater than zero");
        }
        if (!projectIds.insert(id).second) {
            throw std::invalid_argument("Project IDs must be unique");
        }
        if (id > maximumId) {
            maximumId = id;
        }
    }

    if (store.nextId <= maximumId) {
        throw std::invalid_argument("nextId must exceed every project ID");
    }
}

std::filesystem::path temporaryFilePath(const std::filesystem::path& targetFile,
                                        const std::filesystem::path& targetDirectory) {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string suffix = std::to_string(timestamp) + "-" + std::to_string(counter++);
    return targetDirectory / (targetFile.filename().string() + ".tmp-" + suffix);
}

}  // namespace

namespace devmanager {

JsonProjectRepository::JsonProjectRepository(std::filesystem::path filePath)
    : JsonProjectRepository(std::move(filePath), std::make_shared<PlatformFileReplacer>()) {
}

JsonProjectRepository::JsonProjectRepository(std::filesystem::path filePath,
                                             std::shared_ptr<FileReplacer> fileReplacer)
    : filePath_(std::move(filePath)), fileReplacer_(std::move(fileReplacer)) {
    if (fileReplacer_ == nullptr) {
        throw std::invalid_argument("File replacer must not be null");
    }
}

ProjectStore JsonProjectRepository::loadStore() const {
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

        validateProjectStore(store);
        return store;
    } catch (const std::exception& error) {
        throw std::runtime_error("Invalid project data file '" + filePath_.string() +
                                 "': " + error.what());
    }
}

void JsonProjectRepository::saveStore(const ProjectStore& store) const {
    try {
        validateProjectStore(store);
    } catch (const std::exception& error) {
        throw std::runtime_error("Invalid project store for '" + filePath_.string() +
                                 "': " + error.what());
    }

    nlohmann::json payload;
    payload["nextId"] = store.nextId;
    payload["projects"] = nlohmann::json::array();
    for (const Project& project : store.projects) {
        payload["projects"].push_back(project.toJson());
    }

    const std::filesystem::path parentDirectory = filePath_.parent_path();
    if (!parentDirectory.empty()) {
        std::filesystem::create_directories(parentDirectory);
    }

    const std::filesystem::path targetDirectory =
        parentDirectory.empty() ? std::filesystem::current_path() : parentDirectory;
    const std::filesystem::path temporaryFile = temporaryFilePath(filePath_, targetDirectory);

    try {
        {
            std::ofstream output(temporaryFile, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("Unable to open temporary project data file for writing");
            }

            output << payload.dump(2) << '\n';
            if (!output) {
                throw std::runtime_error("Unable to write temporary project data file");
            }

            output.close();
            if (!output) {
                throw std::runtime_error("Unable to close temporary project data file");
            }
        }

        fileReplacer_->replace(temporaryFile, filePath_);
    } catch (const std::exception& error) {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryFile, cleanupError);
        std::string message = "Unable to save project data file '" + filePath_.string() +
                              "': " + error.what();
        if (cleanupError) {
            message += "; unable to remove temporary file: " + cleanupError.message();
        }
        throw std::runtime_error(message);
    }
}

}  // namespace devmanager
