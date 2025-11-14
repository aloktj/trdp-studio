cmake_minimum_required(VERSION 3.16)
project(trdp_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(SQLite3 REQUIRED)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)

FetchContent_MakeAvailable(cpp_httplib)

set(TRDP_APP_SOURCES
    src/main.cpp
    src/db/Database.cpp
    src/auth/AuthService.cpp
    src/auth/PasswordHasher.cpp
    src/auth/AuthManager.cpp
    src/http/JsonUtils.cpp
    src/http/HttpRouter.cpp
    src/trdp/TrdpEngine.cpp
    src/trdp/ConfigService.cpp
    src/trdp/TrdpConfigService.cpp
    src/network/NetworkConfigService.cpp
    src/util/Logger.cpp
)

add_executable(trdp_app ${TRDP_APP_SOURCES})

if(TARGET httplib)
    target_link_libraries(trdp_app PRIVATE httplib)
else()
    target_include_directories(trdp_app PRIVATE ${cpp_httplib_SOURCE_DIR})
endif()

target_include_directories(trdp_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(SQLite3_FOUND)
    target_link_libraries(trdp_app PRIVATE SQLite::SQLite3)
endif()

if(UNIX)
    target_link_libraries(trdp_app PRIVATE dl)
endif()

add_compile_definitions(HTTPLIB_USE_POLL=1)
