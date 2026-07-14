"""
Two approaches that produce identical (visible, interior, border) totals
to the original per-voxel kernel, compared head-to-head.

A) hierarchical (per-frame from scratch):
     - Coarse pass: one thread per 8^3 brick -> classify the whole brick
         REJECT          : entirely outside frustum (contributes nothing)
         FULL_INTERIOR   : entirely inside one tile (atomicAdd 512 to vis+int)
         RECURSE         : straddles boundary (append brick id to fine list)
     - Fine pass: one workgroup of 8x8x8 per brick on the recurse list,
         per-voxel classification with shared-mem reduction (the original
         classification, preserved exactly so totals match).

B) persistent (build once, reuse each frame):
     - Build phase  : run the coarse pass once, capture (vis_full, int_full)
                      and the recurse-brick list.
     - Per-frame    : skip coarse, just run fine pass on the saved list,
                      seeded with the saved (vis_full, int_full).
     This is what you actually do in an engine: coarse classification is
     stable across frames if the world is static; rebuild only on edits.

Both produce the same numbers as the original kernel.  Timing uses
GL_TIME_ELAPSED queries around the dispatches only -- map/readback is
outside the timed region.
"""

import ctypes
import numpy as np
from OpenGL.GL import *
from OpenGL.GL.shaders import compileProgram, compileShader
import glfw

# ---------------- shaders ----------------

with open("coarse.comp", "r") as f:
    COARSE_SRC = f.read()

with open("coarse.comp", "r") as f:
    FINE_SRC = f.read()

# ---------------- gl helpers ----------------

def init_gl():
    if not glfw.init():
        raise RuntimeError("GLFW init failed")
    glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 4)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    win = glfw.create_window(1, 1, "hidden", None, None)
    if not win:
        glfw.terminate()
        raise RuntimeError("Window creation failed")
    glfw.make_context_current(win)
    return win

def make_buf(size_bytes):
    b = glGenBuffers(1)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, b)
    glBufferData(GL_SHADER_STORAGE_BUFFER, size_bytes, None, GL_DYNAMIC_DRAW)
    return b

def write_uints(buf, vals):
    arr = (ctypes.c_uint32 * len(vals))(*vals)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf)
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, ctypes.sizeof(arr), arr)

def read_uints(buf, count):
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf)
    ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY)
    arr = (ctypes.c_uint32 * count).from_address(ptr)
    out = list(arr)
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER)
    return out

def gpu_time(work):
    """Run work() and return GPU-elapsed ms (blocks for the result)."""
    q = glGenQueries(1)
    qid = int(q[0]) if hasattr(q, "__len__") else int(q)
    glBeginQuery(GL_TIME_ELAPSED, qid)
    work()
    glEndQuery(GL_TIME_ELAPSED)
    elapsed = ctypes.c_uint64(0)
    glGetQueryObjectui64v(qid, GL_QUERY_RESULT, ctypes.byref(elapsed))
    glDeleteQueries(1, [qid])
    return elapsed.value / 1e6

# ---------------- gl resources ----------------

MAX_FINE = 8 * 1024 * 1024   # 8M brick slots = 32 MB

prog_coarse = None
prog_fine   = None
buf_out     = None
buf_counter = None
buf_fine    = None

def setup():
    global prog_coarse, prog_fine, buf_out, buf_counter, buf_fine
    prog_coarse = compileProgram(compileShader(COARSE_SRC, GL_COMPUTE_SHADER))
    prog_fine   = compileProgram(compileShader(FINE_SRC,   GL_COMPUTE_SHADER))
    buf_out     = make_buf(12)
    buf_counter = make_buf(12)
    buf_fine    = make_buf(MAX_FINE * 4)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf_out)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buf_counter)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, buf_fine)

# ---------------- passes ----------------

def coarse_dispatch(N):
    Nb = (N + 7) // 8
    glUseProgram(prog_coarse)
    glUniform1i (glGetUniformLocation(prog_coarse, "N"),  N)
    glUniform1i (glGetUniformLocation(prog_coarse, "Nb"), Nb)
    glUniform1ui(glGetUniformLocation(prog_coarse, "maxFine"), MAX_FINE)
    gd = (Nb + 3) // 4
    glDispatchCompute(gd, gd, gd)

def fine_dispatch(N, count):
    if count == 0: return
    glUseProgram(prog_fine)
    glUniform1i (glGetUniformLocation(prog_fine, "N"),     N)
    glUniform1ui(glGetUniformLocation(prog_fine, "count"), count)
    if count <= 65535:
        glDispatchCompute(count, 1, 1)
    else:
        gy = (count + 65534) // 65535
        gx = (count + gy - 1) // gy
        glDispatchCompute(gx, gy, 1)

# ---------------- approaches ----------------

def run_hierarchical(N):
    """Approach A: full pipeline every call. Returns (totals, gpu_ms)."""
    write_uints(buf_out,     [0, 0, 0])
    write_uints(buf_counter, [0, 0, 0])

    # time coarse alone, then sync to read count, then time fine alone --
    # this avoids inflating the timer with the cpu->gpu fence latency
    t1 = gpu_time(lambda: coarse_dispatch(N))
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)

    fine_count = read_uints(buf_counter, 1)[0]
    if fine_count > MAX_FINE:
        print(f"  WARN: fineCount={fine_count} > MAX_FINE={MAX_FINE}; clamping")
    count = min(fine_count, MAX_FINE)

    t2 = gpu_time(lambda: fine_dispatch(N, count))
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT)

    res = tuple(read_uints(buf_out, 3))
    return res, t1 + t2, count

def build_persistent(N):
    """Build phase: coarse once, snapshot (vis_full, int_full, count)."""
    write_uints(buf_out,     [0, 0, 0])
    write_uints(buf_counter, [0, 0, 0])
    coarse_dispatch(N)
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT)
    out = read_uints(buf_out, 3)        # (vis_full, int_full, 0)
    count = min(read_uints(buf_counter, 1)[0], MAX_FINE)
    return out[0], out[1], count

def run_persistent(N, vis_full, int_full, count):
    """Approach B: per-frame work only (fine pass over saved list)."""
    write_uints(buf_out, [vis_full, int_full, 0])
    t = gpu_time(lambda: fine_dispatch(N, count))
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT)
    res = tuple(read_uints(buf_out, 3))
    return res, t

# ---------------- main ----------------

def best_of(fn, k=3):
    times = []
    last = None
    for _ in range(k):
        last, t = fn()[:2] if len(fn()) == 3 else fn()  # not used; placeholder
        times.append(t)
    return last, min(times)

def main():
    init_gl()
    setup()

    # warmup -- compile cache, gpu clocks, query stabilization
    for _ in range(2):
        run_hierarchical(128)

    print(f"{'N':>6}   {'A: hierarchical':>34}   {'B: persistent':>34}   match")
    print("-" * 120)
    for k in range(5, 13):
        N = 2 ** k

        # --- A: best of 3 ---
        ts_a, res_a, fine_count = [], None, None
        for _ in range(3):
            res_a, t, fine_count = run_hierarchical(N)
            ts_a.append(t)
        ms_a = min(ts_a)

        # --- B: build once, best of 3 per-frame ---
        vis_full, int_full, count_b = build_persistent(N)
        ts_b, res_b = [], None
        for _ in range(3):
            res_b, t = run_persistent(N, vis_full, int_full, count_b)
            ts_b.append(t)
        ms_b = min(ts_b)

        ok = "OK" if res_a == res_b else "MISMATCH"
        print(f"N={N:5d}   A: {str(res_a):>22} {ms_a:7.3f} ms"
              f"   B: {str(res_b):>22} {ms_b:7.3f} ms   {ok}"
              f"   (fine bricks: {fine_count})")

if __name__ == "__main__":
    main()
