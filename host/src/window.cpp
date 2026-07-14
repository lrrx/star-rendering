#include "window.hpp"

#include <iostream>
#include <filesystem>

#include <glad/gl.h>

class WindowHandlerCallbacks {
public:
    static void frambuffer_size(GLFWwindow* glfwWindow, int width, int height) {
        glViewport(0, 0, width, height);
    }

    static void key(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods) {
        Window* instance = reinterpret_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
        if (action == GLFW_PRESS){
            //ensure we don't get weird key ids
            assert(key >= 0);
            assert(key < instance->mKeyJustPressedArray.size());
            assert(key < instance->mKeyDownArray.size());

            instance->mKeyJustPressedArray[key] = true;
            instance->mKeyDownArray[key] = true;
        }

        if (action == GLFW_RELEASE){
            //ensure we don't get weird key ids
            assert(key >= 0);
            assert(key < instance->mKeyDownArray.size());
            instance->mKeyDownArray[key] = false;
        }
    }

    static void mouse(GLFWwindow* glfwWindow, double xposIn, double yposIn) {
        Window* instance = reinterpret_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
        instance->mCursorPosition = {xposIn, yposIn};
    }

    static void error(int error, const char* description)
    {
        std::cerr << "[GL ERROR]: " << error << "\t" << description << std::endl;
    }
};

Window::Window(glm::uvec2 size, bool fullscreen, int swapInterval) :
    mSize{size}
{
    std::cout << "Current working directory: " << std::filesystem::current_path() << std::endl;

    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        std::quick_exit(-1);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_POSITION_X, 0);
    glfwWindowHint(GLFW_POSITION_Y, 0);

    mWindowHandle = glfwCreateWindow(mSize.x, mSize.y,
        "CPU Points + GPU Software Rasterizer", 
        glfwGetPrimaryMonitor(),
        nullptr);
    if (!mWindowHandle) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        std::quick_exit(-1);
    }

    glfwMakeContextCurrent(mWindowHandle);

    //pass instance, for now we use this class as a singleton only, but who knows
    glfwSetWindowUserPointer(mWindowHandle, this);

    if(!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD" << std::endl;
        std::quick_exit(-1);
    }

    glfwSetFramebufferSizeCallback(mWindowHandle, WindowHandlerCallbacks::frambuffer_size);
    glfwSetErrorCallback(WindowHandlerCallbacks::error);
    glfwSetCursorPosCallback(mWindowHandle, WindowHandlerCallbacks::mouse);
    glfwSetKeyCallback(mWindowHandle, WindowHandlerCallbacks::key);
    glfwSetInputMode(mWindowHandle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glfwSwapInterval(0);
}

bool Window::wasKeyJustPressed(int key)  {
    if (key >= 0 && key < static_cast<int>(mKeyJustPressedArray.size())) 
        return mKeyJustPressedArray[key];
    return false;
}

bool Window::isKeyDown(int key)  {
    if (key >= 0 && key < static_cast<int>(mKeyDownArray.size())) 
        return mKeyDownArray[key];
    return false;
}

std::array<bool, 512> const& Window::getKeyJustPressedArray() const {return mKeyJustPressedArray;}
std::array<bool, 512> const& Window::getKeyDownArray() const {return mKeyDownArray;}

glm::vec2 Window::getCursorPosition() {
    return mCursorPosition;
}

void Window::clear() {
    glClear(GL_COLOR_BUFFER_BIT);
}

void Window::finishFrame() {
    glfwSwapBuffers(mWindowHandle);
}

void Window::resetKeys() {
    mKeyJustPressedArray.fill(false);
}

void Window::requestToClose() {
    glfwSetWindowShouldClose(mWindowHandle, true);
}

bool Window::isClosing() {
    return glfwWindowShouldClose(mWindowHandle);
}

glm::uvec2 Window::getSize() {
    return mSize;
}

Window::~Window() {
    glfwDestroyWindow(mWindowHandle);
    glfwTerminate();
}