#include <iostream>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <array>
#include <sstream>

#include <glm/glm.hpp>

#include "window.hpp"

#include <newstar.hpp>
#include "camera/camera_controller.hpp"
#include "filesystem"

#include <glm/gtc/matrix_inverse.hpp>

int main(int argc, char** argv) {
    Window window{{3440, 1440}, false, 0};
    CameraController camera{};

    size_t WARP_SIZE = 128;
    size_t CHUNK_SIZE = WARP_SIZE * 128;

    if(argc > 1) {
        size_t const WARP_SIZE = std::stoi(argv[1]);
        size_t const CHUNK_SIZE = std::stoi(argv[2]);
    }

    newstar_init(window.getSize());
    newstar_setdata({});
    //std::unique_ptr<StarRenderer> computeProgram = std::make_unique<StarRenderer>(camera, window);
    
    glm::uvec2 windowSize = window.getSize();

    int width = windowSize.x;
    int height = windowSize.y;

    size_t framecount = 0;
    std::array<float, 1000> rendertimes{};

    camera.cameraPos = glm::vec3(-600, 4800, -700);
    camera.pitch = -85.f;
    camera.yaw = 0.f;

    float lastFrameTimestamp = glfwGetTime();
    // Main loop
    while (!window.isClosing()) {
        window.clear();

        float currentFrame = glfwGetTime();
        float delta = currentFrame - lastFrameTimestamp;
        lastFrameTimestamp = currentFrame;

        glfwPollEvents();

        

        if(window.wasKeyJustPressed(GLFW_KEY_0)) camera.cameraPos = glm::vec3(0.0);
        if(window.wasKeyJustPressed(GLFW_KEY_9)) camera.cameraPos = glm::vec3(3000, 40000, 0);
        if(window.wasKeyJustPressed(GLFW_KEY_7)) camera.cameraPos = glm::vec3(3000, 100000, 0);
        if(window.wasKeyJustPressed(GLFW_KEY_8)) {
            camera.cameraPos = glm::vec3(-600, 4800, -700);
            camera.pitch = -85.f;
            camera.yaw = 0.f;
        }

        camera.process_keyboard(window, delta);
        camera.handleMousePosition(window.getCursorPosition());

        //render, measure time
        double renderStartTimestamp = glfwGetTime();
        {
            glm::mat4 matMV = camera.get_view();
            glm::mat4 matP = camera.get_projection();            

            newstar_render(
                matMV,
                matP,
                1e2,
                false,
                window.getKeyJustPressedArray(),
                window.getKeyDownArray()
            );
        }
        
        double renderTimeDelta = glfwGetTime() - renderStartTimestamp;
        rendertimes[framecount % rendertimes.size()] = renderTimeDelta;

        framecount++;
        static size_t repeat_no = 0;
        if(framecount % rendertimes.size() == 0 && (repeat_no++ > 4)) {
            double avg = std::accumulate(rendertimes.begin(), rendertimes.end(), 0.0) / rendertimes.size();
            double min = *std::min_element(rendertimes.begin(), rendertimes.end());
            double max = *std::max_element(rendertimes.begin(), rendertimes.end());
            std::cout << "compute time (ms): " << std::endl;
            std::cout << WARP_SIZE << "\t" << CHUNK_SIZE << "\t"<< avg * 1000.0f << std::endl;
            //std::cout << "    min time: " << min * 1000.0f << " ms" << std::endl;
            //std::cout << "    max time: " << max * 1000.0f << " ms" << std::endl;
            /*std::cout << "    camera.z: " << camera.cameraPos.z << std::endl;
            for(auto const& x: {avg, min, max, gScale}) {
                std::cout << static_cast<uint64_t>(x * 1'000'000.0) << "\t";
            }
            std::cout << std::endl;

            if(globalScale < 0.001f) exit(-1);
            //exit(0);*/
        }

        window.resetKeys();
        window.finishFrame();
    }

    //manual cleanup, TODO: RAII wrappers for other gl objects
    newstar_delete();

    std::cout << "exiting" << std::endl;
    return 0;
}