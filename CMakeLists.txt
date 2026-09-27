cmake_minimum_required(VERSION 3.16)

project(CSC481-581-Group4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(
    libzmq
    GIT_REPOSITORY https://github.com/zeromq/libzmq.git
    GIT_TAG v4.3.5
)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_DRAFTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(libzmq)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

# Keep the original connectivity smoke-test around; harmless to leave in.
add_executable(hello hello.cpp)
target_link_libraries(hello PRIVATE SDL3::SDL3)

# --- Milestone 1 additions ---
add_subdirectory(Engine)
add_subdirectory(Demo)

# Each teammate gets their own subfolder under Games/ with its own
# CMakeLists.txt, so everyone's individual game builds as a separate
# executable against the shared Engine library.
add_subdirectory(Games/skaveti)
add_subdirectory(Games/ymherya)
add_subdirectory(Games/ashah)

add_executable(engine_server
    ServerMain.cpp
    Engine/src/Server.cpp
)

target_include_directories(engine_server PRIVATE
    Engine/include
)

target_link_libraries(engine_server PRIVATE
    engine
)

add_executable(network_test
    NetworkTest.cpp
)

target_include_directories(network_test PRIVATE
    Engine/include
)

target_link_libraries(network_test PRIVATE
    engine
    libzmq
)