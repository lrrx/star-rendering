import numpy as np
from OpenGL.GL import *
from OpenGL.GL.shaders import compileProgram, compileShader
import glfw
import math

# -----------------------------
# Compute shader source
# -----------------------------
with open("voxel.comp", "r") as f:
    COMPUTE_SRC = f.read()

def init_gl():
    if not glfw.init():
        raise RuntimeError("GLFW init failed")

    glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
    win = glfw.create_window(1, 1, "hidden", None, None)
    glfw.make_context_current(win)
    return win

def run_gpu(N):
    win = init_gl()

    # Output buffer: [visible, interior, border]
    out_buf = glGenBuffers(1)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_buf)
    glBufferData(GL_SHADER_STORAGE_BUFFER, 3 * 4, None, GL_DYNAMIC_COPY)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, out_buf)

    # Compile compute shader
    prog = compileProgram(
        compileShader(COMPUTE_SRC, GL_COMPUTE_SHADER)
    )
    glUseProgram(prog)

    # Push uniform N
    loc = glGetUniformLocation(prog, "N")
    glUniform1i(loc, N)

    # Dispatch: 1 thread per voxel
    groups = (N, N, N)
    glDispatchCompute(*groups)

    # Ensure GPU finished
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)

    # Read back results
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_buf)
    ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY)
    arr = np.frombuffer((ctypes.c_uint32 * 3).from_address(ptr), dtype=np.uint32)
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER)

    glfw.terminate()
    return arr[0], arr[1], arr[2]

if __name__ == "__main__":
    for k in range(5, 14):  # N = 32..512
        N = 2 ** k
        print(f"Running GPU for N={N}")
        visible, interior, border = run_gpu(N)
        print("visible:", visible)
        print("interior:", interior)
        print("border:", border)
        print()


