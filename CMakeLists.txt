cmake_minimum_required(VERSION 3.16)

project(CSC481-581-Group4)
include(CTest)

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

add_executable(hello hello.cpp)
target_link_libraries(hello PRIVATE SDL3::SDL3)

add_subdirectory(Engine)


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
    Tests/NetworkTest.cpp
)

target_include_directories(network_test PRIVATE
    Engine/include
)

target_link_libraries(network_test PRIVATE
    engine
    libzmq
)

add_executable(object_model_test
    Tests/ObjectModelTest.cpp
)

target_include_directories(object_model_test PRIVATE
    Engine/include
)

target_link_libraries(object_model_test PRIVATE
    engine
)

add_executable(object_systems_test
    Tests/ObjectSystemsTest.cpp
)

target_include_directories(object_systems_test PRIVATE
    Engine/include
)

target_link_libraries(object_systems_test PRIVATE
    engine
)

add_executable(camera_test
    Tests/CameraTest.cpp
)

target_include_directories(camera_test PRIVATE
    Engine/include
)

target_link_libraries(camera_test PRIVATE
    engine
)

# --- Milestone 3 networking ---
add_executable(net_sync_test
    Tests/NetSyncTest.cpp
    Engine/src/Server.cpp
)

target_include_directories(net_sync_test PRIVATE
    Engine/include
)

target_link_libraries(net_sync_test PRIVATE
    engine
)

# Headless client used by the Part 1C performance experiments.
add_executable(net_bench_client
    Tests/NetBenchClient.cpp
)

target_link_libraries(net_bench_client PRIVATE
    engine
)

add_executable(gameplay_systems_test
    Tests/GamePlaySystemsTest.cpp
)

target_include_directories(gameplay_systems_test PRIVATE
    Engine/include
)

target_link_libraries(gameplay_systems_test PRIVATE
    engine
)

if(BUILD_TESTING)
    add_executable(event_manager_test Tests/EventManagerTest.cpp)
    target_link_libraries(event_manager_test PRIVATE engine)
    add_test(NAME event_manager_test COMMAND event_manager_test)
    add_executable(replay_test Tests/ReplayTest.cpp)
    target_link_libraries(replay_test PRIVATE engine)
    add_test(NAME replay_test COMMAND replay_test)
    add_executable(engine_events_test Tests/EngineEventsTest.cpp)
    target_link_libraries(engine_events_test PRIVATE engine)
    add_test(NAME engine_events_test COMMAND engine_events_test)
    set_tests_properties(engine_events_test PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
        TIMEOUT 15
    )
    add_test(NAME object_model_test COMMAND object_model_test)
    add_test(NAME object_systems_test COMMAND object_systems_test)
    add_test(NAME camera_test COMMAND camera_test)
    add_test(NAME gameplay_systems_test COMMAND gameplay_systems_test)
    add_test(NAME net_sync_test COMMAND net_sync_test)
endif()
