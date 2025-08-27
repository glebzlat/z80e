set(PY_MODULE z80py)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
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
