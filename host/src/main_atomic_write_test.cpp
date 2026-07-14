#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <array>
#include <sstream>
#include <fstream>
#include <chrono>

#include <glm/glm.hpp>

// ============================================================================
// CONFIGURATION CONSTANTS (change these to test different scenarios)
// ============================================================================

// Resolution to test
constexpr int TEST_WIDTH = 1920;
constexpr int TEST_HEIGHT = 1080;

// Work group size
constexpr uint32_t LOCAL_SIZE_X = 16;
constexpr uint32_t LOCAL_SIZE_Y = 16;

// Number of atomic operations per thread
constexpr uint32_t ATOMIC_COUNT = 256;

// Number of frames to average
constexpr int NUM_FRAMES = 100;

// ============================================================================
// ENABLE/DISABLE FULL CONFIGURATION TESTING
// ============================================================================

// #define RUN_FULL_CONFIGURATION_TEST 1

// ============================================================================
// SHADER SOURCE CODE (embedded to avoid external file dependencies)
// ============================================================================

const char* CLEAR_SHADER_SOURCE = R"(
#version 460 core

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0, r32f) uniform image2D uColorBuffer;

uniform ivec2 uResolution;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if(pixel.x >= uResolution.x || pixel.y >= uResolution.y) return;
    imageStore(uColorBuffer, pixel, vec4(0.0));
}
)";

const char* ATOMIC_WRITE_SHADER_SOURCE = R"(
#version 460 core
#extension GL_NV_shader_atomic_float : enable

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, r32f) uniform image2D uColorBuffer;

uniform ivec2 uResolution;
uniform uint uAtomicCount;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if(pixel.x >= uResolution.x || pixel.y >= uResolution.y) return;
    float value = 1.0;
    for(uint i = 0; i < uAtomicCount; i++) {
        imageAtomicAdd(uColorBuffer, pixel, value);
    }
}
)";

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

GLuint createShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if(!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation failed: " << infoLog << std::endl;
        return 0;
    }
    return shader;
}

GLuint createComputeProgram(const char* source) {
    GLuint shader = createShader(GL_COMPUTE_SHADER, source);
    if(shader == 0) return 0;
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if(!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Program linking failed: " << infoLog << std::endl;
        return 0;
    }

    glDeleteShader(shader);
    return program;
}

GLuint createImage2D(int width, int height, GLenum format) {
    GLuint image;
    glGenTextures(1, &image);
    glBindTexture(GL_TEXTURE_2D, image);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return image;
}

std::vector<double> runTest(int width, int height, uint32_t atomicCount,
                           uint32_t localSizeX, uint32_t localSizeY,
                           int numFrames = 100) {
    std::vector<double> frameTimes;
    
    // Create image
    GLuint image = createImage2D(width, height, GL_R32F);

    // Create programs
    GLuint clearProgram = createComputeProgram(CLEAR_SHADER_SOURCE);
    GLuint atomicProgram = createComputeProgram(ATOMIC_WRITE_SHADER_SOURCE);

    if(clearProgram == 0 || atomicProgram == 0) {
        std::cerr << "Failed to create programs" << std::endl;
        return frameTimes;
    }

    // Set up image binding
    glBindImageTexture(0, image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);

    // Calculate dispatch sizes
    uint32_t dispatchX = (width + localSizeX - 1) / localSizeX;
    uint32_t dispatchY = (height + localSizeY - 1) / localSizeY;

    // Warm-up run (discard results)
    glUseProgram(atomicProgram);
    glUniform2i(glGetUniformLocation(atomicProgram, "uResolution"), width, height);
    glUniform1ui(glGetUniformLocation(atomicProgram, "uAtomicCount"), atomicCount);
    glDispatchCompute(dispatchX, dispatchY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glFinish();  // Wait for warm-up to complete

    // Run test frames with CPU timing (more reliable than GPU timestamps)
    for(int i = 0; i < numFrames; i++) {
        // Clear buffer
        glUseProgram(clearProgram);
        glUniform2i(glGetUniformLocation(clearProgram, "uResolution"), width, height);
        glDispatchCompute(dispatchX, dispatchY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        // Atomic write test with CPU timing
        auto start = std::chrono::high_resolution_clock::now();
        
        glUseProgram(atomicProgram);
        glUniform2i(glGetUniformLocation(atomicProgram, "uResolution"), width, height);
        glUniform1ui(glGetUniformLocation(atomicProgram, "uAtomicCount"), atomicCount);
        glDispatchCompute(dispatchX, dispatchY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        
        // Wait for GPU to complete this frame before measuring
        glFinish();
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;
        double ms = duration.count();
        
        // Sanity check - if time is too large, something is wrong
        if(ms > 100.0) {
            std::cerr << "Warning: Unusually large time: " << ms << " ms" << std::endl;
        }
        
        frameTimes.push_back(ms);
    }

    // Cleanup
    glDeleteProgram(clearProgram);
    glDeleteProgram(atomicProgram);
    glDeleteTextures(1, &image);

    return frameTimes;
}

void printResults(const std::string& testName, const std::vector<double>& times,
                 uint32_t width, uint32_t height, uint32_t atomicCount,
                 uint32_t localSizeX, uint32_t localSizeY) {
    if(times.empty()) {
        std::cout << testName << ": No data" << std::endl;
        return;
    }

    // Calculate average time
    double sum = 0.0;
    double min_ms = times[0];
    double max_ms = times[0];
    for(const auto& t : times) {
        sum += t;
        if(t < min_ms) min_ms = t;
        if(t > max_ms) max_ms = t;
    }
    double avg_ms = sum / times.size();
    
    // Calculate total threads and atomics per frame
    uint32_t dispatchX = (width + localSizeX - 1) / localSizeX;
    uint32_t dispatchY = (height + localSizeY - 1) / localSizeY;
    uint64_t totalThreads = (uint64_t)dispatchX * dispatchY * localSizeX * localSizeY;
    uint64_t atomicsPerFrame = totalThreads * atomicCount;
    
    // Calculate throughput: atomics per 4ms (4ms is a common frame time target)
    double atomicsPerMs = (double)atomicsPerFrame / avg_ms;
    double atomicsPer4ms = atomicsPerMs * 4.0;
    
    // Calculate bandwidth estimate (4 bytes per float atomic)
    double bytesPer4ms = atomicsPer4ms * 4.0;  // 4 bytes per r32f
    double GBs = (bytesPer4ms * 1e6) / 1e9;  // Convert to GB/s (4ms = 0.004s)
    
    std::cout << testName << ":" << std::endl;
    std::cout << "  Avg time: " << avg_ms * 1000.0 << " ms" << std::endl;
    std::cout << "  Min time: " << min_ms * 1000.0 << " ms" << std::endl;
    std::cout << "  Max time: " << max_ms * 1000.0 << " ms" << std::endl;
    std::cout << "  Resolution: " << width << "x" << height << std::endl;
    std::cout << "  Total threads: " << totalThreads << std::endl;
    std::cout << "  Atomics/frame: " << atomicsPerFrame / 1000000.0 << "M" << std::endl;
    std::cout << "  Atomics/4ms: " << atomicsPer4ms / 1000000.0 << "M" << std::endl;
    std::cout << "  Bandwidth: " << GBs << " GB/s (estimated)" << std::endl;
    std::cout << std::endl;
}

// ============================================================================
// FULL CONFIGURATION TESTING (DISABLED BY DEFAULT)
// ============================================================================

#ifdef RUN_FULL_CONFIGURATION_TEST

struct TestConfig {
    std::string name;
    uint32_t atomicCount;
    uint32_t localSizeX;
    uint32_t localSizeY;
    int numFrames;
    int width;
    int height;
};

void runFullConfigurationTests() {
    std::vector<TestConfig> configs = {
        // Atomic count scaling test
        {"1 atomic/thread", 1, 16, 16, 50, 1920, 1080},
        {"16 atomics/thread", 16, 16, 16, 50, 1920, 1080},
        {"64 atomics/thread", 64, 16, 16, 50, 1920, 1080},
        {"256 atomics/thread", 256, 16, 16, 50, 1920, 1080},
        {"1024 atomics/thread", 1024, 16, 16, 50, 1920, 1080},

        // Work group size comparison
        {"16x16 work group", 256, 16, 16, 50, 1920, 1080},
        {"32x32 work group", 256, 32, 32, 50, 1920, 1080},
        {"8x8 work group", 256, 8, 8, 50, 1920, 1080},
        {"64x16 work group", 256, 64, 16, 50, 1920, 1080},

        // Resolution scaling
        {"1080p (1920x1080)", 256, 16, 16, 50, 1920, 1080},
        {"1440p (2560x1440)", 256, 16, 16, 50, 2560, 1440},
        {"4K (3840x2160)", 256, 16, 16, 50, 3840, 2160},
    };

    std::cout << "Running full configuration tests..." << std::endl;
    std::cout << "Each test runs " << configs[0].numFrames << " frames (averaging)" << std::endl;
    std::cout << std::endl;

    for(const auto& config : configs) {
        std::cout << "Testing: " << config.name << "..." << std::endl;

        auto times = runTest(config.width, config.height, config.atomicCount,
                           config.localSizeX, config.localSizeY, config.numFrames);
        printResults(config.name, times, config.width, config.height, 
                    config.atomicCount, config.localSizeX, config.localSizeY);

        // Small delay between tests
        glfwPollEvents();
    }
}

#endif // RUN_FULL_CONFIGURATION_TEST

// ============================================================================
// MAIN PROGRAM
// ============================================================================

int main() {
    // Initialize GLFW
    if(!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Set OpenGL version
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // Hidden window for compute-only test

    // Create window
    GLFWwindow* window = glfwCreateWindow(TEST_WIDTH, TEST_HEIGHT, "RTX 4070 Compute Shader Test", nullptr, nullptr);
    if(!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    // Initialize GLAD
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "=== RTX 4070 Compute Shader Atomic Write Test ===" << std::endl;
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "GPU: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "Resolution: " << TEST_WIDTH << "x" << TEST_HEIGHT << std::endl;
    std::cout << "Work Group: " << LOCAL_SIZE_X << "x" << LOCAL_SIZE_Y << std::endl;
    std::cout << "Atomics/Thread: " << ATOMIC_COUNT << std::endl;
    std::cout << "Frames to Average: " << NUM_FRAMES << std::endl;
    std::cout << std::endl;

    // Check for atomic float support
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    bool hasAtomicFloat = extensions && std::string(extensions).find("GL_NV_shader_atomic_float") != std::string::npos;
    std::cout << "GL_NV_shader_atomic_float: " << (hasAtomicFloat ? "YES" : "NO") << std::endl;
    std::cout << std::endl;

    // Run single test or full configuration suite
#ifdef RUN_FULL_CONFIGURATION_TEST
    runFullConfigurationTests();
#else
    std::cout << "Running single test configuration..." << std::endl;
    std::cout << "To run full configuration tests, uncomment #define RUN_FULL_CONFIGURATION_TEST" << std::endl;
    std::cout << std::endl;

    auto times = runTest(TEST_WIDTH, TEST_HEIGHT, ATOMIC_COUNT,
                        LOCAL_SIZE_X, LOCAL_SIZE_Y, NUM_FRAMES);
    printResults("Current Configuration", times, TEST_WIDTH, TEST_HEIGHT,
                ATOMIC_COUNT, LOCAL_SIZE_X, LOCAL_SIZE_Y);
#endif

    // Summary
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Expected RTX 4070 limits:" << std::endl;
    std::cout << "  Memory Bandwidth: ~504 GB/s (theoretical)" << std::endl;
    std::cout << "  VRAM: 12GB GDDR6X" << std::endl;
    std::cout << "  Compute Cores: 5888 CUDA" << std::endl;
    std::cout << std::endl;
    std::cout << "Interpretation:" << std::endl;
    std::cout << "  - < 100M atomics/4ms at 1080p = Compute bound" << std::endl;
    std::cout << "  - > 200M atomics/4ms at 1080p, < 100M at 4K = Bandwidth bound" << std::endl;
    std::cout << "  - 256 atomics/thread should saturate memory at 4K" << std::endl;
    std::cout << "  - Expected bandwidth: 350-450 GB/s (realistic)" << std::endl;

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << std::endl << "Test complete!" << std::endl;
    return 0;
}