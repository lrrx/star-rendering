import ctypes
import numpy as np
from OpenGL.GL import *
from OpenGL.GL.shaders import compileProgram, compileShader
import glfw

def init_gl():
    if not glfw.init():
        raise RuntimeError("GLFW init failed")
    glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
    win = glfw.create_window(1, 1, "hidden", None, None)
    glfw.make_context_current(win)
    # Load shader
    with open("voxel_opt.comp", "r") as f:
        src = f.read()
    prog = compileProgram(compileShader(src, GL_COMPUTE_SHADER))
    glUseProgram(prog)
    return prog, win

def run_gpu(N):
    prog, win = init_gl()
    # SSBO for [visible, interior, border]
    buf = glGenBuffers(1)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf)
    glBufferData(GL_SHADER_STORAGE_BUFFER, 3 * 4, None, GL_DYNAMIC_DRAW)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf)
    # Zero it
    zero = (ctypes.c_uint32 * 3)(0, 0, 0)
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, ctypes.sizeof(zero), zero)
    glUniform1i(glGetUniformLocation(prog, "N"), N)
    # Workgroup size = 8x8x8, so dispatch ceil(N/8)
    gx = (N + 7) // 8
    gy = (N + 7) // 8
    gz = (N + 7) // 8
    # --- GPU timer query around the dispatch ---
    q = glGenQueries(1)
    qid = int(q[0]) if hasattr(q, "__len__") else int(q)
    glBeginQuery(GL_TIME_ELAPSED, qid)
    glDispatchCompute(gx, gy, gz)
    glEndQuery(GL_TIME_ELAPSED)
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)
    # GL_QUERY_RESULT blocks until the GPU is done — gives true elapsed ns
    elapsed_ns = ctypes.c_uint64(0)
    glGetQueryObjectui64v(qid, GL_QUERY_RESULT, ctypes.byref(elapsed_ns))
    gpu_ms = elapsed_ns.value / 1e6
    glDeleteQueries(1, [qid])
    # Read back
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf)
    ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY)
    data = (ctypes.c_uint32 * 3).from_address(ptr)
    arr = np.frombuffer(data, dtype=np.uint32).copy()
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER)
    glfw.terminate()
    return int(arr[0]), int(arr[1]), int(arr[2]), gpu_ms
if __name__ == "__main__":
    run_gpu(16)
    print("             visible\tinside\tborder")
    for k in range(5, 13):  # 32 .. 4096
        N = 2 ** k
        v, i, b, ms = run_gpu(N)
        n = N**3
        print(f"N = {N:4d}-> ({v/n:.3f},\t{i/v:.3f},\t{b/v:.3f})  gpu={int(ms * 1000):8d} us \t = ({v:10d},\t{i:10d},\t{b:10d})")

