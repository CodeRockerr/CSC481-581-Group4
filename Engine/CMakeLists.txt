find_package(SDL3_image REQUIRED CONFIG)

add_library(engine STATIC
    src/Window.cpp
    src/Renderer.cpp
    src/Entity.cpp
    src/EntityManager.cpp
    src/Physics.cpp
    src/Input.cpp
    src/Collision.cpp
    src/Engine.cpp
)

target_include_directories(engine PUBLIC include)

target_link_libraries(engine PUBLIC
    SDL3::SDL3
    SDL3_image::SDL3_image
)