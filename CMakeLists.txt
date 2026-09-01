cmake_minimum_required(VERSION 3.16)

project(CSC481-581-Group4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
# Keep the original connectivity smoke-test around; harmless to leave in.
add_executable(hello hello.cpp)
target_link_libraries(hello PRIVATE SDL3::SDL3)

# --- Milestone 1 additions ---
add_subdirectory(Engine)

# Each teammate gets their own subfolder under Games/ with its own
# CMakeLists.txt, so everyone's individual game builds as a separate
# executable against the shared Engine library.
add_subdirectory(Games/saisumedh)
# add_subdirectory(Games/Teammate2)
# add_subdirectory(Games/Teammate3)
