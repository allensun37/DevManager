#include "repository/FileReplacer.h"

#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace devmanager {

void PlatformFileReplacer::replace(const std::filesystem::path& temporaryFile,
                                   const std::filesystem::path& targetFile) const {
#ifdef _WIN32
    std::error_code error;
    const bool targetExists = std::filesystem::exists(targetFile, error);
    if (error) {
        throw std::filesystem::filesystem_error("Unable to inspect project data file",
                                                targetFile,
                                                error);
    }

    const std::wstring temporaryPath = temporaryFile.wstring();
    const std::wstring targetPath = targetFile.wstring();
    if (targetExists) {
        if (!ReplaceFileW(targetPath.c_str(), temporaryPath.c_str(), nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "Unable to replace project data file");
        }
        return;
    }

    if (!MoveFileExW(temporaryPath.c_str(), targetPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Unable to move project data file into place");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporaryFile, targetFile, error);
    if (error) {
        throw std::filesystem::filesystem_error("Unable to replace project data file",
                                                temporaryFile,
                                                targetFile,
                                                error);
    }
#endif
}

}  // namespace devmanager
