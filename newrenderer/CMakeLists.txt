cmake_minimum_required(VERSION 3.20)
project(NewStarRenderer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT TARGET glm::glm)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

# Private vendored glad2-MX loader. OBJECT library so the symbols fold
# directly into NewStarRenderer with no public target the host can see.
add_library(newstar_glad_private OBJECT
    external/glad/src/gl.c
)
target_include_directories(newstar_glad_private PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
)

add_library(NewStarRenderer
    src/StarRenderer.cpp
    src/gl/shader_utils.cpp
    src/util/util.cpp
    src/gl/GpuTexture.cpp
    src/newstar.cpp
    src/stars/stars.cpp
    src/preprocessing/preprocessing.cpp
    src/preprocessing/datatypes.cpp
    $<TARGET_OBJECTS:newstar_glad_private>
)

target_include_directories(NewStarRenderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
)

target_link_libraries(NewStarRenderer PUBLIC glm::glm)

if(UNIX AND NOT APPLE)
    target_link_libraries(NewStarRenderer PRIVATE GL dl)
endif()

add_executable(NewStarRendererTests
    tests/src/main.cpp
)

target_link_libraries(NewStarRendererTests
    PRIVATE NewStarRenderer
)

target_include_directories(NewStarRendererTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)