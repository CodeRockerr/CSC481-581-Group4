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
    add_executable(ashah_m4_test Milestone4Test.cpp ${CMAKE_SOURCE_DIR}/Engine/src/Server.cpp)
    target_link_libraries(ashah_m4_test PRIVATE engine)
    add_test(NAME ashah_m4_test COMMAND ashah_m4_test)
    add_test(NAME ashah_sonar_binary_test COMMAND ashah_m4_test --network binary)
    add_test(NAME ashah_sonar_text_test COMMAND ashah_m4_test --network text)
    set_tests_properties(ashah_m4_test ashah_sonar_binary_test ashah_sonar_text_test PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
        TIMEOUT 20
    )
    set_tests_properties(ashah_route_test ashah_pause_test ashah_foundations_test PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
        TIMEOUT 90
    )
endif()
