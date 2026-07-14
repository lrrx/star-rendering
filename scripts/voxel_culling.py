import math

# Camera / FOV / tiling setup
FOV_H = math.radians(90.0)
ASPECT = 16.0 / 9.0
FOV_V = 2.0 * math.atan((1.0 / ASPECT) * math.tan(FOV_H / 2.0))

TILES_X = 30
TILES_Y = 17

DELTA_X = FOV_H / TILES_X
DELTA_Y = FOV_V / TILES_Y

HALF_FOV_H = FOV_H / 2.0
HALF_FOV_V = FOV_V / 2.0

def corner_coords(i, j, k, N):
    """Return the 8 corner coordinates of voxel (i,j,k) in world space."""
    x0 = i - N / 2.0
    x1 = x0 + 1.0
    y0 = j - N / 2.0
    y1 = y0 + 1.0
    z0 = k - N / 2.0
    z1 = z0 + 1.0
    return [
        (x0, y0, z0),
        (x0, y0, z1),
        (x0, y1, z0),
        (x0, y1, z1),
        (x1, y0, z0),
        (x1, y0, z1),
        (x1, y1, z0),
        (x1, y1, z1),
    ]

def project_to_tile(x, y, z):
    """Project a point to tile indices (tx, ty), or return None if outside FOV or behind camera."""
    if z <= 0.0:
        return None
    theta_x = math.atan(x / z)
    theta_y = math.atan(y / z)

    # Outside FOV?
    if theta_x <= -HALF_FOV_H or theta_x >= HALF_FOV_H:
        return None
    if theta_y <= -HALF_FOV_V or theta_y >= HALF_FOV_V:
        return None

    tx = int((theta_x + HALF_FOV_H) / DELTA_X)
    ty = int((theta_y + HALF_FOV_V) / DELTA_Y)

    # Clamp just in case of floating point edge
    if tx < 0 or tx >= TILES_X or ty < 0 or ty >= TILES_Y:
        return None

    return (tx, ty)

def classify_voxel(i, j, k, N):
    """
    Classify voxel (i,j,k) as:
      - None: not visible (no corner in front of camera & inside FOV)
      - "interior": all visible corners in same tile
      - "border": visible corners in more than one tile
    """
    tiles = set()
    for (x, y, z) in corner_coords(i, j, k, N):
        t = project_to_tile(x, y, z)
        if t is not None:
            tiles.add(t)

    if not tiles:
        return None  # not visible

    if len(tiles) == 1:
        return "interior"
    else:
        return "border"

def run_for_N(N):
    total_voxels = N ** 3
    visible = 0
    interior = 0
    border = 0

    for i in range(N):
        for j in range(N):
            for k in range(N):
                cls = classify_voxel(i, j, k, N)
                if cls is None:
                    continue
                visible += 1
                if cls == "interior":
                    interior += 1
                else:
                    border += 1

    print(f"N = {N}")
    print(f"  total voxels:   {total_voxels}")
    print(f"  visible voxels: {visible}")
    print(f"  interior:       {interior}")
    print(f"  border:         {border}")
    print()

if __name__ == "__main__":
    for k in range(5, 11):  # N = 2^5 .. 2^10
        N = 2 ** k
        # WARNING: N=256 and especially N=512,1024 will be extremely slow with this naive triple loop.
        # Start with N=32,64,128 and see how far your machine can go.
        run_for_N(N)

