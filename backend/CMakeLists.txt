cmake_minimum_required(VERSION 3.16)
project(trdp_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)
find_package(SQLite3 REQUIRED)

option(TRDP_AUTO_SETUP "Download and stage the TRDP stack during configure" OFF)
set(TRDP_SETUP_PREFIX "${CMAKE_BINARY_DIR}/trdp-install" CACHE PATH
    "Install prefix used when TRDP_AUTO_SETUP is enabled")
set(TRDP_PLATFORM_CONFIG "LINUX_X86_64_config" CACHE STRING
    "Upstream Makefile target that selects the TRDP platform configuration")

if(TRDP_AUTO_SETUP)
    set(_TRDP_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/scripts/setup_trdp.sh")
    if(NOT EXISTS "${_TRDP_SETUP_SCRIPT}")
        message(FATAL_ERROR "TRDP_AUTO_SETUP is enabled but ${_TRDP_SETUP_SCRIPT} is missing")
    endif()

    if(NOT TRDP_ROOT)
        set(TRDP_ROOT "${TRDP_SETUP_PREFIX}" CACHE PATH "Root directory of the TRDP installation" FORCE)
    endif()

    set(_TRDP_ARCHIVE "${TRDP_ROOT}/lib/libtrdp.a")
    if(NOT EXISTS "${_TRDP_ARCHIVE}")
        message(STATUS "Auto-installing TRDP into ${TRDP_ROOT}")
        execute_process(
            COMMAND "${_TRDP_SETUP_SCRIPT}" "${TRDP_ROOT}" "${TRDP_PLATFORM_CONFIG}"
            RESULT_VARIABLE _TRDP_SETUP_RESULT
        )
        if(NOT _TRDP_SETUP_RESULT EQUAL 0)
            message(FATAL_ERROR "TRDP auto setup failed with exit code ${_TRDP_SETUP_RESULT}")
        endif()
    else()
        message(STATUS "Reusing existing TRDP install at ${TRDP_ROOT}")
    endif()
endif()

find_package(TRDP REQUIRED)

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
    src/util/LogService.cpp
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

if(TARGET TRDP::trdp)
    target_link_libraries(trdp_app PRIVATE TRDP::trdp)
endif()

if(TARGET TRDP::trdpap)
    target_link_libraries(trdp_app PRIVATE TRDP::trdpap)
endif()

if(SQLite3_FOUND)
    target_link_libraries(trdp_app PRIVATE SQLite::SQLite3)
endif()

if(UNIX)
    target_link_libraries(trdp_app PRIVATE dl)
endif()

add_compile_definitions(HTTPLIB_USE_POLL=1)
