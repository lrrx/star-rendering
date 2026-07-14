cmake_minimum_required(VERSION 3.20)
project(OpenGLHost LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# Host's own loader — completely independent of the library's vendored copy.
add_library(host_glad STATIC external/glad/src/gl.c)
target_include_directories(host_glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
)

find_package(OpenGL REQUIRED)

add_executable(OpenGLHost
    src/main.cpp
    src/window.cpp
    src/camera/camera_controller.cpp
)

target_link_libraries(OpenGLHost PRIVATE
    NewStarRenderer
    host_glad
    glfw
    glm::glm
    OpenGL::GL
)

if(UNIX AND NOT APPLE)
    target_link_libraries(OpenGLHost PRIVATE pthread dl)
endif()