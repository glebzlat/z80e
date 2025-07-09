add_library(utils utils/linkedlist.c)
target_link_directories(utils PUBLIC utils)
target_compile_options(utils PUBLIC -Wall -Wpedantic -Wextra -g)

add_executable(z80test z80test.c)
target_link_libraries(z80test z80e utils)

find_program(PYTHON python3)

add_custom_target(
    RunInstructionTest ALL
    ${PYTHON} -m unittest tests.test_instructions
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_custom_target(
    RunDAATest
    ${PYTHON} -m tests.test_daa
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
