cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(Z80E_TESTING "Compile tests" OFF)
option(Z80E_PYTHON_MODULE "Compile Python library" OFF)

project(z80e LANGUAGES C)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCE_DIR}/source/z80/emulator.c)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/source)
target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wpedantic)
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    target_compile_definitions(${PROJECT_NAME} PRIVATE BIG_ENDIAN)
elseif(CMAKE_C_BYTE_ORDER STREQUAL "LITTLE_ENDIAN")
    target_compile_definitions(${PROJECT_NAME} PRIVATE LITTLE_ENDIAN)
else()
    message(SEND_ERROR "C byte order is not known")
endif()

set(PY_MODULE z80py)
Python3_add_library(${PY_MODULE} MODULE WITH_SOABI EXCLUDE_FROM_ALL
    ${PROJECT_SOURCE_DIR}/source/z80py/emulator.c
)
target_include_directories(${PY_MODULE} PUBLIC
    ${PROJECT_SOURCE_DIR}/source/z80py
    ${Python3_INCLUDE_DIRS}
)
target_link_libraries(${PY_MODULE} PUBLIC ${Python3_LIBRARIES} ${PROJECT_NAME})
set_target_properties(${PY_MODULE} PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
target_compile_options(${PY_MODULE} PUBLIC -Wall -Wpedantic)

set(VENV_DIR ${PROJECT_SOURCE_DIR}/.env)
set(VENV_STAMP ${VENV_DIR}/env.dev.stamp)
set(DEV_REQUIREMENTS ${PROJECT_SOURCE_DIR}/requirements.dev.txt)

if(Z80E_PYTHON_MODULE OR Z80E_TESTING)
    add_custom_target(BuildZ80Py ALL DEPENDS ${PY_MODULE})
    install(TARGETS ${PY_MODULE} DESTINATION .)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR Z80E_TESTING)
    message(STATUS "Enable debug symbols")
    target_compile_options(${PROJECT_NAME} PUBLIC -g)
    target_compile_options(${PY_MODULE} PUBLIC -g)
endif()

if(Z80E_TESTING)
    message(STATUS "Enable testing")
    add_subdirectory(${PROJECT_SOURCE_DIR}/tests)
endif()
