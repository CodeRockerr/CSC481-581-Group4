add_executable(ashah_game main.cpp)
target_include_directories(ashah_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ashah_game PRIVATE engine)

add_executable(ashah_server server.cpp ${CMAKE_SOURCE_DIR}/Engine/src/Server.cpp)
target_include_directories(ashah_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Engine/include
)
target_link_libraries(ashah_server PRIVATE engine)

if(BUILD_TESTING)
    add_executable(ashah_gameplay_test GameplayTest.cpp)
    target_link_libraries(ashah_gameplay_test PRIVATE engine)
    add_test(NAME ashah_gameplay_test COMMAND ashah_gameplay_test)
    add_test(NAME ashah_route_test COMMAND ashah_game --verify-run)
    add_test(NAME ashah_pause_test COMMAND ashah_game --verify-pause)
    add_test(NAME ashah_foundations_test COMMAND ashah_game --verify-foundations)
    set_tests_properties(ashah_route_test ashah_pause_test ashah_foundations_test PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
        TIMEOUT 90
    )
endif()
