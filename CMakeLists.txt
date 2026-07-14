cmake_minimum_required(VERSION 3.20)
project(MySplitProject LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Only fetch glm if the parent project hasn't already provided it.
# CosmoScout already has glm; our standalone build needs to fetch it.
if(NOT TARGET glm::glm)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

add_subdirectory(newrenderer)

# Only build the test host when this is the top-level project.
# When CosmoScout pulls us in, CMAKE_SOURCE_DIR is CosmoScout's root,
# not ours, so this skips building the host.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(host)
endif()
