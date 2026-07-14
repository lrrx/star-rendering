// camera_controller.cpp
#include "camera_controller.hpp"
#include <GLFW/glfw3.h>

#include <iostream>

constexpr double cParsecToMeter = 3.08567758e16;

void CameraController::process_keyboard(Window& window, float deltaTime) {
    if (window.isKeyDown(GLFW_KEY_ESCAPE))
        window.requestToClose();

    double speedFactor = 1000.0;
    if(window.isKeyDown(GLFW_KEY_LEFT_SHIFT))
        speedFactor *= 10.0;
    
    if(window.isKeyDown(GLFW_KEY_LEFT_CONTROL))
        speedFactor /= 10.f;

    double cameraSpeed = static_cast<float>(speedFactor * deltaTime);
    if (window.isKeyDown(GLFW_KEY_W))
        cameraPos += cameraSpeed * cameraFront;
    
    if (window.isKeyDown(GLFW_KEY_S))
        cameraPos -= cameraSpeed * cameraFront;
    if (window.isKeyDown(GLFW_KEY_A))
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (window.isKeyDown(GLFW_KEY_D))
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (window.isKeyDown(GLFW_KEY_Q))
        cameraPos += cameraSpeed * glm::highp_f64vec3(0,1,0);
    if (window.isKeyDown(GLFW_KEY_E))
        cameraPos -= cameraSpeed * glm::highp_f64vec3(0,1,0);
}

void CameraController::handleMousePosition(glm::vec2 cursorPosition)
{    
    static bool firstMouse = true;
    float xpos = static_cast<float>(cursorPosition.x);
    float ypos = static_cast<float>(cursorPosition.y);

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f; // change this value to your liking
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    // make sure that when pitch is out of bounds, screen doesn't get flipped
    if (pitch > 85.0f)
        pitch = 85.0f;
    if (pitch < -85.0f)
        pitch = -85.0f;

    glm::highp_f64vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}


glm::mat4 CameraController::get_view() const {
    auto view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    auto scaled = glm::scale(view, glm::highp_f64vec3(1 / cParsecToMeter));

    return scaled;
}

glm::mat4 CameraController::get_projection() const {
    float aspect = 21.f / 9.f; //TODO: make dynamic

    glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, 0.1f, 9999999999999.f);

    return projection;
}

CameraController::CameraController() {

}