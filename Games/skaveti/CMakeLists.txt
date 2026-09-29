add_executable(skaveti_game main.cpp)
target_link_libraries(skaveti_game PRIVATE engine)

add_executable(skaveti_server server.cpp ${CMAKE_SOURCE_DIR}/Engine/src/Server.cpp)
target_include_directories(skaveti_server PRIVATE ${CMAKE_SOURCE_DIR}/Engine/include)
target_link_libraries(skaveti_server PRIVATE engine)
