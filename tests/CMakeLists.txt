add_library(utils utils/linkedlist.c)
target_link_directories(utils PUBLIC utils)
target_compile_options(utils PUBLIC -Wall -Wpedantic -Wextra -g)

add_executable(z80test z80test.c)
target_link_libraries(z80test z80e utils)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(
    OUTPUT ${VENV_DIR}
    COMMAND ${Python3_EXECUTABLE} -m venv ${VENV_DIR}
)

add_custom_command(
    OUTPUT ${VENV_STAMP}
    DEPENDS ${VENV_DIR}
    COMMAND ${Python3_EXECUTABLE} -m pip --python ${VENV_DIR}/bin/python
        install -r ${DEV_REQUIREMENTS}
        --upgrade --report=${VENV_STAMP}
)

add_custom_target(
    RunInstructionTest ALL
    ${Python3_EXECUTABLE} -m unittest tests.test_instructions
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    DEPENDS ${VENV_STAMP}
)

add_custom_target(
    RunDAATest
    ${Python3_EXECUTABLE} -m tests.test_daa
    DEPENDS ${VENV_STAMP}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
