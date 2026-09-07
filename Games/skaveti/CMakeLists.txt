# Rename "your_name_game" to something like "adit_game" / "yeva_game" etc.
# so each teammate's executable is distinct.
add_executable(skaveti_game main.cpp)
target_link_libraries(skaveti_game PRIVATE engine)