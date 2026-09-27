add_library(engine STATIC
    src/Window.cpp
    src/Renderer.cpp
    src/Entity.cpp
    src/EntityManager.cpp
    src/Physics.cpp
    src/Input.cpp
    src/Collision.cpp
    src/Engine.cpp
    src/Image.cpp
    src/Timeline.cpp
    src/FrameWorker.cpp
    src/NetworkClient.cpp
)

target_include_directories(engine PUBLIC include PRIVATE ../vendored/SDL/src/video)

target_link_libraries(engine PUBLIC SDL3::SDL3 libzmq)