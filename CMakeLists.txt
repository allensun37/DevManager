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

add_executable(DevManager src/main.cpp)
target_link_libraries(DevManager PRIVATE nlohmann_json::nlohmann_json)

enable_testing()
add_test(NAME DevManagerSmoke COMMAND DevManager)
set_tests_properties(DevManagerSmoke PROPERTIES
    PASS_REGULAR_EXPRESSION "DevManager project skeleton is ready"
)

add_executable(DevManagerProjectTests tests/domain/ProjectTests.cpp)
target_link_libraries(DevManagerProjectTests PRIVATE DevManagerDomain)
add_test(NAME DevManagerProjectTests COMMAND DevManagerProjectTests)

add_executable(DevManagerProjectManagerTests
    tests/application/ProjectManagerTests.cpp
)
target_link_libraries(DevManagerProjectManagerTests PRIVATE DevManagerApplication)
add_test(NAME DevManagerProjectManagerTests COMMAND DevManagerProjectManagerTests)
