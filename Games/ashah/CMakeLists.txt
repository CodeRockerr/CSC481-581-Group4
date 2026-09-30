add_executable(ashah_game main.cpp)
target_link_libraries(ashah_game PRIVATE engine)

add_executable(ashah_server server.cpp ${CMAKE_SOURCE_DIR}/Engine/src/Server.cpp)
target_include_directories(ashah_server PRIVATE ${CMAKE_SOURCE_DIR}/Engine/include)
target_link_libraries(ashah_server PRIVATE engine)
