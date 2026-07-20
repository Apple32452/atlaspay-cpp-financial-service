cmake_minimum_required(VERSION 3.20)
project(atlaspay VERSION 1.0.0 LANGUAGES CXX)

option(ATLASPAY_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

add_library(atlaspay_core
    src/database.cpp
    src/risk_engine.cpp
    src/payment_service.cpp
)

target_include_directories(atlaspay_core PUBLIC include)
target_link_libraries(atlaspay_core PUBLIC SQLite::SQLite3 Threads::Threads)
target_compile_options(atlaspay_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(atlaspay_server
    src/main.cpp
    src/http_server.cpp
)
target_link_libraries(atlaspay_server PRIVATE atlaspay_core)
target_compile_options(atlaspay_server PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(ATLASPAY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target atlaspay_core atlaspay_server)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

enable_testing()
add_executable(atlaspay_tests tests/payment_service_tests.cpp)
target_link_libraries(atlaspay_tests PRIVATE atlaspay_core)
target_compile_options(atlaspay_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME atlaspay_tests COMMAND atlaspay_tests)

if(ATLASPAY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(atlaspay_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(atlaspay_tests PRIVATE -fsanitize=address,undefined)
endif()

install(TARGETS atlaspay_server RUNTIME DESTINATION bin)
