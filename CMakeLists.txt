cmake_minimum_required(VERSION 3.20)

project(
    AtlasPay
    VERSION 1.0.0
    LANGUAGES CXX
)

# ------------------------------------------------------------
# C++ configuration
# ------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# ------------------------------------------------------------
# AtlasPay core library
# ------------------------------------------------------------

add_library(atlaspay_core STATIC
    src/database.cpp
    src/payment_service.cpp
    src/risk_engine.cpp
)

target_include_directories(atlaspay_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlaspay_core
    PUBLIC
        SQLite3::SQLite3
        Threads::Threads
)

# Helpful compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(atlaspay_core
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

# ------------------------------------------------------------
# AtlasPay HTTP server
# ------------------------------------------------------------

add_executable(atlaspay_server
    src/main.cpp
    src/http_server.cpp
)

target_link_libraries(atlaspay_server
    PRIVATE
        atlaspay_core
        Threads::Threads
)

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

enable_testing()

add_executable(atlaspay_tests
    tests/payment_service_tests.cpp
)

target_link_libraries(atlaspay_tests
    PRIVATE
        atlaspay_core
        Threads::Threads
)

add_test(
    NAME atlaspay_payment_service_tests
    COMMAND atlaspay_tests
)

# ------------------------------------------------------------
# Build information
# ------------------------------------------------------------

message(STATUS "AtlasPay configuration")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  SQLite3 version: ${SQLite3_VERSION}")
