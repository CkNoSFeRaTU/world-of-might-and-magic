cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

set(BIN_CODEGEN_SOURCES
        CodeGen.cpp
        CodeGenOptions.cpp)

set(BIN_CODEGEN_HEADERS
        CodeGenOptions.h)

if(NOT BUILD_PLATFORM STREQUAL "android")
    add_executable(CodeGen ${BIN_CODEGEN_SOURCES} ${BIN_CODEGEN_HEADERS})
    target_fix_libcxx_assertions(CodeGen)
    target_link_libraries(CodeGen PUBLIC application CLI11::CLI11)
    target_check_style(CodeGen)
endif()
