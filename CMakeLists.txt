cmake_minimum_required(VERSION 3.20)

project(market_microstructure_lab
    VERSION 0.1.0
    DESCRIPTION "C++20 limit order book and market microstructure simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MML_BUILD_TESTS "Build unit tests" ON)
option(MML_BUILD_BENCHMARKS "Build benchmark suite" ON)
option(MML_BUILD_APPS "Build demo and experiment executables" ON)
option(MML_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MML_NATIVE "Optimise for the host CPU (-march=native)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(mml_options INTERFACE)
if(MSVC)
    target_compile_options(mml_options INTERFACE /W4 /permissive-)
else()
    target_compile_options(mml_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
    if(MML_SANITIZE)
        target_compile_options(mml_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(mml_options INTERFACE -fsanitize=address,undefined)
    endif()
    if(MML_NATIVE)
        target_compile_options(mml_options INTERFACE -march=native)
    endif()
endif()

add_library(mml
    src/engine/matching_engine.cpp
    src/engine/order_gateway.cpp
    src/portfolio/portfolio.cpp
    src/analytics/stats.cpp
    src/sim/order_flow.cpp
    src/sim/market_simulator.cpp
    src/sim/impact_model.cpp
    src/exec/almgren_chriss.cpp
    src/exec/algorithms.cpp
    src/exec/execution_agent.cpp
    src/mm/signals.cpp
    src/mm/market_maker.cpp
    src/mm/markout.cpp
)
add_library(mml::mml ALIAS mml)
target_include_directories(mml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mml PUBLIC mml_options)

if(MML_BUILD_TESTS)
    enable_testing()
    add_executable(mml_tests
        tests/test_main.cpp
        tests/test_order_book.cpp
        tests/test_fuzz.cpp
        tests/test_engine.cpp
        tests/test_portfolio.cpp
        tests/test_simulation.cpp
        tests/test_execution.cpp
        tests/test_market_making.cpp
    )
    target_link_libraries(mml_tests PRIVATE mml)
    add_test(NAME mml_tests COMMAND mml_tests)
endif()

if(MML_BUILD_BENCHMARKS)
    add_executable(mml_bench bench/bench_orderbook.cpp)
    target_link_libraries(mml_bench PRIVATE mml)
endif()

if(MML_BUILD_APPS)
    add_executable(mml_demo apps/demo.cpp)
    target_link_libraries(mml_demo PRIVATE mml)
    add_executable(mml_execution apps/execution_experiment.cpp)
    target_link_libraries(mml_execution PRIVATE mml)
    add_executable(mml_market_making apps/market_making_experiment.cpp)
    target_link_libraries(mml_market_making PRIVATE mml)
endif()
