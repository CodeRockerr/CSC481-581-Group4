add_executable(ymherya_game main.cpp)
target_link_libraries(ymherya_game PRIVATE engine)

add_executable(ymherya_server server.cpp ${CMAKE_SOURCE_DIR}/Engine/src/Server.cpp)
target_include_directories(ymherya_server PRIVATE ${CMAKE_SOURCE_DIR}/Engine/include)
target_link_libraries(ymherya_server PRIVATE engine)

if(BUILD_TESTING)
    add_executable(ymherya_rescue_test RescueTrackerTest.cpp)
    add_test(NAME ymherya_rescue_test COMMAND ymherya_rescue_test)
endif()