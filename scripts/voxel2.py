import numpy as np
from OpenGL.GL import *
from OpenGL.GL.shaders import compileProgram, compileShader
import glfw
import ctypes

def init_gl():
    glfw.init()
    glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
    win = glfw.create_window(1, 1, "hidden", None, None)
    glfw.make_context_current(win)
    return win

def run_gpu(N):
    win = init_gl()

    # Output buffer
    out_buf = glGenBuffers(1)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_buf)
    glBufferData(GL_SHADER_STORAGE_BUFFER, 3 * 4, None, GL_DYNAMIC_COPY)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, out_buf)

    # Load shader
    with open("voxel2.comp") as f:
        src = f.read()

    prog = compileProgram(compileShader(src, GL_COMPUTE_SHADER))
    glUseProgram(prog)

    glUniform1i(glGetUniformLocation(prog, "N"), N)

    # Dispatch: one workgroup per k-slice
    glDispatchCompute(N, 1, 1)
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)

    # Read back
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_buf)
    ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY)
    arr = np.frombuffer((ctypes.c_uint32 * 3).from_address(ptr), dtype=np.uint32)
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER)

    glfw.terminate()
    return arr.tolist()

if __name__ == "__main__":
    for k in range(5, 14):
        N = 2 ** k
        print("N =", N, "→", run_gpu(N))

