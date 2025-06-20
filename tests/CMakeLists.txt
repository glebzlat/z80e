add_executable(z80test z80test.c)
target_link_libraries(z80test z80e)

find_program(PYTHON python3)

add_custom_target(
    RunInstructionTest ALL
    ${PYTHON} -m unittest tests.test_instructions
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
