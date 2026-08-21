cmake_minimum_required(VERSION 3.20)

project(atlaspay VERSION 1.0.0 LANGUAGES CXX)

option(
    ATLASPAY_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
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

# Support both newer and older CMake SQLite target names.
if(TARGET SQLite3::SQLite3)
    set(ATLASPAY_SQLITE_TARGET SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    set(ATLASPAY_SQLITE_TARGET SQLite::SQLite3)
else()
    message(FATAL_ERROR "No supported SQLite3 CMake target was found")
endif()

# ------------------------------------------------------------
# Core library
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
        ${ATLASPAY_SQLITE_TARGET}
        Threads::Threads
)

# ------------------------------------------------------------
# Compiler warnings
# ------------------------------------------------------------

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(atlaspay_core
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

# ------------------------------------------------------------
# HTTP server
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
    NAME atlaspay_tests
    COMMAND atlaspay_tests
)

# ------------------------------------------------------------
# Benchmarks
# ------------------------------------------------------------

add_executable(atlaspay_benchmark
    benchmarks/transaction_benchmark.cpp
)

target_link_libraries(atlaspay_benchmark
    PRIVATE
        atlaspay_core
        Threads::Threads
)

# ------------------------------------------------------------
# Sanitizers
# ------------------------------------------------------------

if(ATLASPAY_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")

        message(STATUS "AtlasPay sanitizers enabled")

        set(ATLASPAY_SANITIZER_FLAGS
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )

        target_compile_options(atlaspay_core
            PRIVATE
                ${ATLASPAY_SANITIZER_FLAGS}
        )

        target_compile_options(atlaspay_server
            PRIVATE
                ${ATLASPAY_SANITIZER_FLAGS}
        )

        target_compile_options(atlaspay_tests
            PRIVATE
                ${ATLASPAY_SANITIZER_FLAGS}
        )

        target_link_options(atlaspay_server
            PRIVATE
                ${ATLASPAY_SANITIZER_FLAGS}
        )

        target_link_options(atlaspay_tests
            PRIVATE
                ${ATLASPAY_SANITIZER_FLAGS}
        )

    else()
        message(
            WARNING
            "ATLASPAY_ENABLE_SANITIZERS is ON, but the compiler is unsupported"
        )
    endif()
endif()

# ------------------------------------------------------------
# Configuration information
# ------------------------------------------------------------

message(STATUS "AtlasPay configuration")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  SQLite version: ${SQLite3_VERSION}")
message(STATUS "  SQLite target: ${ATLASPAY_SQLITE_TARGET}")
message(STATUS "  Sanitizers: ${ATLASPAY_ENABLE_SANITIZERS}")
