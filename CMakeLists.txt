cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(Z80E_TESTING "Compile tests" OFF)
option(Z80E_PYTHON_MODULE "Compile Python library" OFF)
option(Z80E_WEBASM "Compile to WebAssembly" OFF)

if(Z80E_WEBASM)
    find_program(CLANG clang REQUIRED)
    find_program(WASM_LD wasm-ld REQUIRED)
    set(CMAKE_C_COMPILER ${CLANG})
    set(CMAKE_C_FLAGS "--target=wasm32 -flto -nostdlib")
    set(CMAKE_SYSTEM_NAME wasm32)
    set(CMAKE_CROSSCOMPILING TRUE)
    set(CMAKE_C_COMPILER_WORKS TRUE CACHE INTERNAL "")
    set(CMAKE_C_BYTE_ORDER LITTLE_ENDIAN)
endif()

project(z80e LANGUAGES C)

set(SOURCES ${PROJECT_SOURCE_DIR}/source/z80/emulator.c)

if(Z80E_WEBASM)
    list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/source/z80wasm/emulator.c)

    add_executable(${PROJECT_NAME} ${SOURCES})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".wasm"
        LINK_FLAGS "-Wl,--no-entry -Wl,--export-all -Wl,--lto-O3"
    )

else()
    add_library(${PROJECT_NAME} STATIC ${SOURCES})
endif()

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

set(VENV_DIR ${PROJECT_SOURCE_DIR}/.env)
set(VENV_STAMP ${VENV_DIR}/env.dev.stamp)
set(DEV_REQUIREMENTS ${PROJECT_SOURCE_DIR}/requirements.dev.txt)

if(Z80E_PYTHON_MODULE OR Z80E_TESTING)
    if(Z80E_WEBASM)
        message(FATAL_ERROR "Z80E_WEBASM cannot be used with Z80E_PYTHON_MODULE or Z80E_TESTING")
    endif()

    include(${PROJECT_SOURCE_DIR}/cmake/z80py)
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
