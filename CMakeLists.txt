cmake_minimum_required(VERSION 3.20)

project(DevManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    # GoogleTest v1.17.0
    GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

add_library(DevManagerDomain src/domain/Project.cpp)
target_include_directories(DevManagerDomain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerDomain PUBLIC nlohmann_json::nlohmann_json)

add_library(DevManagerApplication src/application/ProjectManager.cpp)
target_include_directories(DevManagerApplication PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerApplication PUBLIC DevManagerDomain)

add_library(DevManagerRepository src/repository/JsonProjectRepository.cpp)
target_include_directories(DevManagerRepository PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerRepository PUBLIC DevManagerDomain)

add_library(DevManagerMenu src/menu/MenuController.cpp)
target_include_directories(DevManagerMenu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerMenu PUBLIC DevManagerApplication)

add_executable(DevManager src/main.cpp)
target_link_libraries(DevManager PRIVATE
    DevManagerMenu
    DevManagerRepository
)

enable_testing()
include(GoogleTest)
add_test(NAME DevManagerSmoke COMMAND DevManager)
set_tests_properties(DevManagerSmoke PROPERTIES
    PASS_REGULAR_EXPRESSION "Goodbye"
)

add_executable(DevManagerProjectTests tests/domain/ProjectTests.cpp)
target_link_libraries(DevManagerProjectTests PRIVATE DevManagerDomain GTest::gtest_main)
gtest_discover_tests(DevManagerProjectTests)

add_executable(DevManagerProjectManagerTests
    tests/application/ProjectManagerTests.cpp
)
target_link_libraries(DevManagerProjectManagerTests PRIVATE
    DevManagerApplication
    DevManagerRepository
    GTest::gtest_main
)
gtest_discover_tests(DevManagerProjectManagerTests)

add_executable(DevManagerJsonProjectRepositoryTests
    tests/repository/JsonProjectRepositoryTests.cpp
)
target_link_libraries(DevManagerJsonProjectRepositoryTests PRIVATE
    DevManagerRepository
    GTest::gtest_main
)
gtest_discover_tests(DevManagerJsonProjectRepositoryTests)

add_executable(DevManagerMenuTests
    tests/menu/MenuControllerTests.cpp
)
target_link_libraries(DevManagerMenuTests PRIVATE DevManagerMenu GTest::gtest_main)
gtest_discover_tests(DevManagerMenuTests)
