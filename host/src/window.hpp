#pragma once

#include <array>

#include <glm/vec2.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

class WindowHandlerCallbacks;

class Window {
public:
    friend class WindowHandlerCallbacks;

    Window(glm::uvec2 size, bool fullscreen, int swapInterval);
    ~Window();

    void resetKeys();

    std::array<bool, 512> const& getKeyJustPressedArray() const;
    std::array<bool, 512> const& getKeyDownArray() const;

public:
    glm::vec2 getCursorPosition();
    bool wasKeyJustPressed(int key);
    bool isKeyDown(int key);

    void requestToClose();
    bool isClosing();

    void clear();
    void finishFrame();

    glm::uvec2 getSize();

private:
    std::array<bool, 512> mKeyJustPressedArray{};
    std::array<bool, 512> mKeyDownArray{};
    glm::vec2 mCursorPosition;

    GLFWwindow* mWindowHandle;

    glm::uvec2 mSize;
};