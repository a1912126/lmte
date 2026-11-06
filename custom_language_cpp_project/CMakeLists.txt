cmake_minimum_required(VERSION 3.16)
project(customlang LANGUAGES C CXX)

find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
)
if(LLVM_CONFIG_EXECUTABLE)
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --cmakedir
        OUTPUT_VARIABLE LLVM_CMAKE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE LLVM_CONFIG_RESULT
    )
    if(NOT LLVM_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to query LLVM's CMake package directory")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${LLVM_CMAKE_DIR}")
endif()
# LLVM is optional: without it everything except --llvm-ir works.
find_package(LLVM CONFIG QUIET)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(customlang
    src/main.cpp
    src/token.cpp
    src/lexer.cpp
    src/value.cpp
    src/parser.cpp
    src/semantic.cpp
    src/optimizer.cpp
    src/interpreter.cpp
    src/ir_generator.cpp
    src/llvm_backend.cpp
)

target_include_directories(customlang PRIVATE include)
target_compile_options(customlang PRIVATE -Wall -Wextra -pedantic -O2)
target_link_libraries(customlang PRIVATE Threads::Threads)

if(LLVM_FOUND)
    target_include_directories(customlang SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    add_definitions(${LLVM_DEFINITIONS})
    llvm_map_components_to_libnames(LLVM_LIBRARIES Core Passes Support)
    target_link_libraries(customlang PRIVATE ${LLVM_LIBRARIES})
else()
    message(STATUS "LLVM not found - building without --llvm-ir support")
    target_compile_definitions(customlang PRIVATE PLAINSPEAK_NO_LLVM)
endif()
