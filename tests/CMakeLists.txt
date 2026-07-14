add_executable(NewStarRendererTests
    src/main.cpp
    src/preprocessing/datatypes.cpp
)

target_link_libraries(NewStarRendererTests
    PRIVATE NewStarRenderer
)

target_include_directories(NewStarRendererTests PRIVATE
    ${CMAKE_SOURCE_DIR}/newrenderer/include
)