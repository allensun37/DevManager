cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(DevManager VERSION 0.5.0 LANGUAGES C CXX)

set(DEVMANAGER_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${DEVMANAGER_GENERATED_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DevManagerVersion.h.in"
    "${DEVMANAGER_GENERATED_INCLUDE_DIR}/DevManagerVersion.h"
    @ONLY
)

set(DEVMANAGER_INIT_MIGRATION_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/database/migrations/001_init.sql"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${DEVMANAGER_INIT_MIGRATION_PATH}"
)
file(READ "${DEVMANAGER_INIT_MIGRATION_PATH}" DEVMANAGER_INIT_MIGRATION_SQL)
string(FIND
    "${DEVMANAGER_INIT_MIGRATION_SQL}"
    ")DMSQL_v1\""
    DEVMANAGER_MIGRATION_DELIMITER_COLLISION
)
if(NOT DEVMANAGER_MIGRATION_DELIMITER_COLLISION EQUAL -1)
    message(FATAL_ERROR "001_init.sql conflicts with the embedded migration delimiter")
endif()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedMigrations.h.in"
    "${DEVMANAGER_GENERATED_INCLUDE_DIR}/EmbeddedMigrations.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

include(FetchContent)
find_package(Threads REQUIRED)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG 095a5c1caf9e467ff840ee2ffd19be1a7852203b
)
# Keep the dependency HTTP-only; cpp-httplib enables OpenSSL when available by default.
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
# MinGW on this toolchain does not expose GetAddrInfoExCancel; use the portable resolver path.
set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpp_httplib)

add_library(CppHttplib INTERFACE)
target_include_directories(CppHttplib INTERFACE
    ${cpp_httplib_SOURCE_DIR}
)
target_link_libraries(CppHttplib INTERFACE httplib)
if(WIN32)
    target_link_libraries(CppHttplib INTERFACE ws2_32)
endif()

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    # GoogleTest v1.17.0
    GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    # Pin the adapter dependency to a released version; never follow master.
    GIT_TAG v1.15.1
)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    sqlite_amalgamation
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
)
FetchContent_MakeAvailable(sqlite_amalgamation)

add_library(DevManagerSqliteC STATIC
    ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c
)
target_include_directories(DevManagerSqliteC PUBLIC
    ${sqlite_amalgamation_SOURCE_DIR}
)
target_link_libraries(DevManagerSqliteC PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
if(UNIX AND NOT APPLE)
    target_link_libraries(DevManagerSqliteC PUBLIC m)
endif()
add_library(SQLite::SQLite3 ALIAS DevManagerSqliteC)

add_library(DevManagerSqliteInfrastructure
    src/infrastructure/sqlite/MigrationManager.cpp
    src/infrastructure/sqlite/SqliteConnection.cpp
    src/infrastructure/sqlite/SqliteStatement.cpp
    src/infrastructure/sqlite/SqliteTransaction.cpp
)
target_include_directories(DevManagerSqliteInfrastructure PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DEVMANAGER_GENERATED_INCLUDE_DIR}
)
target_link_libraries(DevManagerSqliteInfrastructure PRIVATE SQLite::SQLite3)

add_library(DevManagerCommon
    src/common/AsciiText.cpp
    src/common/ProjectIdParser.cpp
    src/common/ProjectSearchText.cpp
)
target_include_directories(DevManagerCommon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(DevManagerConfig src/config/Config.cpp)
target_include_directories(DevManagerConfig PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerConfig PUBLIC nlohmann_json::nlohmann_json)

add_library(DevManagerLogger src/infrastructure/logging/Logger.cpp)
target_include_directories(DevManagerLogger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerLogger PUBLIC spdlog::spdlog)

add_library(DevManagerDomain src/domain/Project.cpp)
target_include_directories(DevManagerDomain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerDomain PUBLIC nlohmann_json::nlohmann_json)

add_library(DevManagerQuery src/query/ProjectQueryEvaluator.cpp)
target_include_directories(DevManagerQuery PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerQuery PUBLIC
    DevManagerCommon
    DevManagerDomain
)

add_library(DevManagerApplication
    src/application/ProjectManager.cpp
    src/application/ApplicationBootstrap.cpp
    src/application/ProjectService.cpp
)
target_include_directories(DevManagerApplication PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerApplication PUBLIC
    DevManagerCommon
    DevManagerDomain
    DevManagerQuery
    DevManagerConfig
    DevManagerLogger
    DevManagerRepository
)

add_library(DevManagerRepository
    src/repository/FileReplacer.cpp
    src/repository/JsonProjectRepository.cpp
    src/repository/ProjectStoreValidator.cpp
    src/repository/SqliteProjectIdCodec.cpp
)
target_include_directories(DevManagerRepository PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerRepository PUBLIC
    DevManagerDomain
    DevManagerQuery
)

add_library(DevManagerSqliteRepository
    src/repository/RepositoryFactory.cpp
    src/repository/SqliteProjectRepository.cpp
)
target_include_directories(DevManagerSqliteRepository PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DEVMANAGER_GENERATED_INCLUDE_DIR}
)
target_link_libraries(DevManagerSqliteRepository PUBLIC
    DevManagerRepository
    DevManagerSqliteInfrastructure
)

# ApplicationBootstrap owns the backend selected by RepositoryFactory, so the
# application library must carry the SQLite repository implementation transitively.
target_link_libraries(DevManagerApplication PUBLIC
    DevManagerSqliteRepository
)

add_library(DevManagerMenu src/menu/MenuController.cpp)
target_include_directories(DevManagerMenu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerMenu PUBLIC DevManagerApplication DevManagerCommon)

add_executable(DevManager src/main.cpp)
target_link_libraries(DevManager PRIVATE
    DevManagerMenu
    DevManagerRepository
)

add_executable(DevManagerHttpServer src/http_main.cpp)
set_target_properties(DevManagerHttpServer PROPERTIES
    OUTPUT_NAME devmanager_http
)
target_link_libraries(DevManagerHttpServer PRIVATE
    DevManagerHttp
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

add_executable(DevManagerConfigTests tests/config/ConfigTests.cpp)
target_include_directories(DevManagerConfigTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerConfigTests PRIVATE
    DevManagerConfig
    GTest::gtest_main
)
gtest_discover_tests(DevManagerConfigTests)

add_executable(DevManagerRepositoryFactoryTests
    tests/repository/RepositoryFactoryTests.cpp
)
target_include_directories(DevManagerRepositoryFactoryTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(DevManagerRepositoryFactoryTests PRIVATE
    DevManagerSqliteRepository
    DevManagerConfig
    SQLite::SQLite3
    GTest::gtest_main
)
gtest_discover_tests(DevManagerRepositoryFactoryTests)

add_executable(DevManagerAsciiTextTests tests/common/AsciiTextTests.cpp)
target_include_directories(DevManagerAsciiTextTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerAsciiTextTests PRIVATE DevManagerCommon GTest::gtest_main)
gtest_discover_tests(DevManagerAsciiTextTests)

add_executable(DevManagerProjectSearchTextTests tests/common/ProjectSearchTextTests.cpp)
target_include_directories(DevManagerProjectSearchTextTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerProjectSearchTextTests PRIVATE
    DevManagerCommon
    GTest::gtest_main
)
gtest_discover_tests(DevManagerProjectSearchTextTests)

add_executable(DevManagerProjectQueryEvaluatorTests
    tests/query/ProjectQueryEvaluatorTests.cpp
)
target_include_directories(DevManagerProjectQueryEvaluatorTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerProjectQueryEvaluatorTests PRIVATE
    DevManagerQuery
    GTest::gtest_main
)
gtest_discover_tests(DevManagerProjectQueryEvaluatorTests)

add_executable(DevManagerProjectIdParserTests tests/common/ProjectIdParserTests.cpp)
target_include_directories(DevManagerProjectIdParserTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerProjectIdParserTests PRIVATE DevManagerCommon GTest::gtest_main)
gtest_discover_tests(DevManagerProjectIdParserTests)

add_executable(DevManagerVersionTests tests/common/DevManagerVersionTests.cpp)
target_include_directories(DevManagerVersionTests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(DevManagerVersionTests PRIVATE GTest::gtest_main)
gtest_discover_tests(DevManagerVersionTests)

add_executable(DevManagerProjectManagerTests
    tests/application/ProjectManagerTests.cpp
)
target_link_libraries(DevManagerProjectManagerTests PRIVATE
    DevManagerApplication
    DevManagerRepository
    GTest::gtest_main
)
gtest_discover_tests(DevManagerProjectManagerTests)

add_executable(DevManagerProjectServiceTests
    tests/application/ProjectServiceTests.cpp
)
target_link_libraries(DevManagerProjectServiceTests PRIVATE
    DevManagerApplication
    GTest::gtest_main
)
gtest_discover_tests(DevManagerProjectServiceTests)

add_executable(DevManagerApplicationBootstrapTests
    tests/application/ApplicationBootstrapTests.cpp
)
target_link_libraries(DevManagerApplicationBootstrapTests PRIVATE
    DevManagerApplication
    DevManagerRepository
    GTest::gtest_main
)
gtest_discover_tests(DevManagerApplicationBootstrapTests)

add_executable(DevManagerLoggerTests
    tests/infrastructure/LoggerTests.cpp
)
target_include_directories(DevManagerLoggerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerLoggerTests PRIVATE
    DevManagerLogger
    GTest::gtest_main
)
gtest_discover_tests(DevManagerLoggerTests)

add_executable(DevManagerJsonProjectRepositoryTests
    tests/repository/JsonProjectRepositoryTests.cpp
)
target_link_libraries(DevManagerJsonProjectRepositoryTests PRIVATE
    DevManagerRepository
    GTest::gtest_main
)
gtest_discover_tests(DevManagerJsonProjectRepositoryTests)

add_executable(DevManagerSqliteProjectIdCodecTests
    tests/repository/SqliteProjectIdCodecTests.cpp
)
target_link_libraries(DevManagerSqliteProjectIdCodecTests PRIVATE
    DevManagerRepository
    SQLite::SQLite3
    GTest::gtest_main
)
gtest_discover_tests(DevManagerSqliteProjectIdCodecTests)

add_executable(DevManagerSqliteProjectRepositoryTests
    tests/repository/SqliteProjectRepositoryTests.cpp
)
target_link_libraries(DevManagerSqliteProjectRepositoryTests PRIVATE
    DevManagerSqliteRepository
    SQLite::SQLite3
    GTest::gtest_main
)
gtest_discover_tests(DevManagerSqliteProjectRepositoryTests)

add_executable(DevManagerProjectRepositoryContractTests
    tests/repository/ProjectRepositoryContractTests.cpp
)
target_link_libraries(DevManagerProjectRepositoryContractTests PRIVATE
    DevManagerSqliteRepository
    SQLite::SQLite3
    GTest::gtest_main
)
gtest_discover_tests(DevManagerProjectRepositoryContractTests)

add_executable(DevManagerSqliteInfrastructureTests
    tests/infrastructure/sqlite/SqliteConnectionTests.cpp
)
target_include_directories(DevManagerSqliteInfrastructureTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerSqliteInfrastructureTests PRIVATE
    DevManagerSqliteInfrastructure
    GTest::gtest_main
)
gtest_discover_tests(DevManagerSqliteInfrastructureTests)

add_executable(DevManagerMigrationTests
    tests/infrastructure/sqlite/MigrationManagerTests.cpp
)
target_link_libraries(DevManagerMigrationTests PRIVATE
    DevManagerSqliteInfrastructure
    GTest::gtest_main
)
gtest_discover_tests(DevManagerMigrationTests)

add_executable(DevManagerMenuTests
    tests/menu/MenuControllerTests.cpp
)
target_link_libraries(DevManagerMenuTests PRIVATE DevManagerMenu GTest::gtest_main)
gtest_discover_tests(DevManagerMenuTests)

add_library(DevManagerHttp
    src/http/HttpError.cpp
    src/http/RequestId.cpp
    src/http/ProjectHttpJsonMapper.cpp
    src/http/HttpServer.cpp
    src/http/ProjectHttpController.cpp
)
target_include_directories(DevManagerHttp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(DevManagerHttp PUBLIC
    DevManagerApplication
    DevManagerRepository
    DevManagerCommon
    DevManagerAuth
    CppHttplib
)

# API-key authentication infrastructure.
add_library(DevManagerAuth STATIC
    src/infrastructure/auth/EnvironmentApiKeyProvider.cpp
    src/infrastructure/auth/ApiKeyAuthenticator.cpp
)
target_include_directories(DevManagerAuth PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(DevManagerApiKeyAuthTests
    tests/infrastructure/auth/EnvironmentApiKeyProviderTests.cpp
    tests/infrastructure/auth/ApiKeyAuthenticatorTests.cpp
)
target_link_libraries(DevManagerApiKeyAuthTests PRIVATE
    DevManagerAuth
    GTest::gtest_main
)
gtest_discover_tests(DevManagerApiKeyAuthTests)

add_executable(DevManagerProjectHttpJsonMapperTests
    tests/http/ProjectHttpJsonMapperTests.cpp
)
target_include_directories(DevManagerProjectHttpJsonMapperTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerProjectHttpJsonMapperTests PRIVATE
    DevManagerHttp
    GTest::gtest_main
)
gtest_discover_tests(DevManagerProjectHttpJsonMapperTests)

add_executable(DevManagerProjectHttpControllerTests
    tests/http/ProjectHttpControllerTests.cpp
)
target_include_directories(DevManagerProjectHttpControllerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(DevManagerProjectHttpControllerTests PRIVATE
    DevManagerHttp
    GTest::gtest_main
)
gtest_discover_tests(DevManagerProjectHttpControllerTests)

add_executable(DevManagerHttpServerIntegrationTests
    tests/http/HttpServerIntegrationTests.cpp
)
target_include_directories(DevManagerHttpServerIntegrationTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(DevManagerHttpServerIntegrationTests PRIVATE
    DevManagerHttp
    CppHttplib
    GTest::gtest_main
)
gtest_discover_tests(DevManagerHttpServerIntegrationTests)
