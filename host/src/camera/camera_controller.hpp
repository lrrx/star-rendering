#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../window.hpp"

struct CameraController{
    CameraController();

    glm::highp_f64vec3 cameraPos   = glm::vec3(3000.0f, 45000.0f, 0.f);
    glm::highp_f64vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::highp_f64vec3 cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);

    float yaw   = -45.f;	// yaw is initialized to -90.0 degrees since a yaw of 0.0 results in a direction vector pointing to the right so we initially rotate a bit to the left.
    float pitch =  -85.f;
    float lastX =  1920.0f / 2.0;
    float lastY =  1080.0 / 2.0;
    float fov   =  45.0f;
    
    void process_keyboard(Window& window, float delta_time);
    void process_mouse_movement(double xpos, double ypos);
    glm::mat4 get_view() const;
    glm::mat4 get_projection() const;

    void handleMousePosition(glm::vec2 cursorPosition);
};