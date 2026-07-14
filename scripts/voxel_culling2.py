import math
from concurrent.futures import ThreadPoolExecutor, as_completed

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

    if tx < 0 or tx >= TILES_X or ty < 0 or ty >= TILES_Y:
        return None

    return (tx, ty)

def classify_voxel(i, j, k, N):
    """
    Classify voxel (i,j,k) as:
      - None: not visible
      - "interior": all visible corners in same tile
      - "border": visible corners in more than one tile
    """
    # Precompute voxel bounds
    x0 = i - N / 2.0
    x1 = x0 + 1.0
    y0 = j - N / 2.0
    y1 = y0 + 1.0
    z0 = k - N / 2.0
    z1 = z0 + 1.0

    tiles = set()

    # 8 corners
    for x in (x0, x1):
        for y in (y0, y1):
            for z in (z0, z1):
                t = project_to_tile(x, y, z)
                if t is not None:
                    tiles.add(t)

    if not tiles:
        return None
    if len(tiles) == 1:
        return "interior"
    return "border"

def process_z_range(N, k_start, k_end):
    """Process voxels with k in [k_start, k_end) for a given N."""
    visible = 0
    interior = 0
    border = 0

    for k in range(k_start, k_end):
        for i in range(N):
            for j in range(N):
                cls = classify_voxel(i, j, k, N)
                if cls is None:
                    continue
                visible += 1
                if cls == "interior":
                    interior += 1
                else:
                    border += 1

    return visible, interior, border

def run_for_N(N, num_workers=8, z_chunk_size=4):
    total_voxels = N ** 3

    # Split k (z index) into chunks
    tasks = []
    for k_start in range(0, N, z_chunk_size):
        k_end = min(N, k_start + z_chunk_size)
        tasks.append((k_start, k_end))

    visible_total = 0
    interior_total = 0
    border_total = 0

    with ThreadPoolExecutor(max_workers=num_workers) as executor:
        futures = [
            executor.submit(process_z_range, N, k_start, k_end)
            for (k_start, k_end) in tasks
        ]
        for fut in as_completed(futures):
            v, inter, bord = fut.result()
            visible_total += v
            interior_total += inter
            border_total += bord

    print(f"N = {N}")
    print(f"  total voxels:   {total_voxels}")
    print(f"  visible voxels: {visible_total}")
    print(f"  interior:       {interior_total}")
    print(f"  border:         {border_total}")
    print()

if __name__ == "__main__":
    # Adjust num_workers to your CPU
    for k in range(5, 11):  # N = 2^5 .. 2^10
        N = 2 ** k
        print(f"Running for N={N} ...")
        run_for_N(N, num_workers=8, z_chunk_size=4)

