cmake_minimum_required(VERSION 3.16)

project(CSC481-581-Group4)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

add_executable(hello hello.cpp)

target_link_libraries(hello PRIVATE SDL3::SDL3)
