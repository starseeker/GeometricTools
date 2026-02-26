cmake_minimum_required(VERSION 3.8)
project(GeometricToolsTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GTE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(test_geogram_port test_geogram_port.cpp)
target_include_directories(test_geogram_port PRIVATE ${GTE_ROOT}/GTE)

enable_testing()
add_test(NAME GeogramPortTests COMMAND test_geogram_port)
