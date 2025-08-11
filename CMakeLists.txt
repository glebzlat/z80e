cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(Z80E_TESTING "Compile tests" OFF)

project(z80e LANGUAGES C)

add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCE_DIR}/source/z80/emulator.c)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/source)
target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wpedantic)

set(VENV_DIR ${PROJECT_SOURCE_DIR}/.env)
set(VENV_STAMP ${VENV_DIR}/env.dev.stamp)
set(DEV_REQUIREMENTS ${PROJECT_SOURCE_DIR}/requirements.dev.txt)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR Z80E_TESTING)
    message(STATUS "Enable debug symbols")
    target_compile_options(${PROJECT_NAME} PUBLIC -g)
endif()

if(Z80E_TESTING)
    message(STATUS "Enable testing")
    target_compile_options(${PROJECT_NAME} PUBLIC -g)
    add_subdirectory(${PROJECT_SOURCE_DIR}/tests)
endif()
