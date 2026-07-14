// ---------------------------------------------------------------
//  Frustum planes are passed as 4‑component vectors:
//  (a,b,c,d) = normal.xyz, distance
// ---------------------------------------------------------------
uniform vec4 uFrustum[6];   // filled by the CPU (see extractFrustumPlanes)

// ----------------------------------------------------------------
//  Test an axis‑aligned box against the frustum.
//  `boxMin` / `boxMax` are world‑space coordinates.
//  Returns true if the box intersects (including full containment).
// ----------------------------------------------------------------
bool aabbIntersectsFrustum(vec3 boxMin, vec3 boxMax, vec4 frustum[6])
{
    for (int i = 0; i < 6; ++i)
    {
        vec4 pl = frustum[i];
        // pick the negative vertex (the one farthest opposite the plane normal)
        vec3 n   = pl.xyz;
        vec3 v   = vec3( (n.x >= 0.0) ? boxMin.x : boxMax.x,
                        (n.y >= 0.0) ? boxMin.y : boxMax.y,
                        (n.z >= 0.0) ? boxMin.z : boxMax.z );

        // if that vertex is outside, the whole box is outside
        if (dot(n, v) + pl.w < 0.0) return false;
    }
    return true; // intersect or fully inside
}

// ---------------------------------------------------------------
//  Build the six view‑frustum planes from a single view‑proj matrix.
//  The planes are stored as (a,b,c,d)  →  a·x + b·y + c·z + d = 0
//  The function returns a 6‑element array of normalized planes.
// ---------------------------------------------------------------
void extractFrustumPlanes(mat4 vp, out vec4 planes[6])
{
    // GLSL stores matrices column‑major, i.e. vp[0] is the first column.
    // To get the rows we can transpose the matrix first.
    mat4 t = transpose(vp);                // now t[0] … t[3] are the rows

    // left   = row3 + row0
    planes[0] = t[3] + t[0];
    // right  = row3 - row0
    planes[1] = t[3] - t[0];
    // bottom = row3 + row1
    planes[2] = t[3] + t[1];
    // top    = row3 - row1
    planes[3] = t[3] - t[1];
    // near   = row3 + row2
    planes[4] = t[3] + t[2];
    planes[4].w += 10.0;
    // far    = row3 - row2
    planes[5] = t[3] - t[2];

    // ----  Normalise each plane (optional but makes the later test cheaper)
    for (int i = 0; i < 6; ++i)
    {
        float invLen = inversesqrt(dot(planes[i].xyz, planes[i].xyz));
        planes[i] *= invLen;
    }
}

    vec4 frustum[6];
    extractFrustumPlanes(uViewProj, frustum);

    // -----------------------------------------------------------
    // 3) AABB Frustum test – voxel is an axis‑aligned cube of size uCellSize
    // -----------------------------------------------------------
    float h = uCellSize * .1f;
    vec3 boxMin = voxelCenter - h;
    vec3 boxMax = voxelCenter + h;

    if (!aabbIntersectsFrustum(boxMin, boxMax, frustum))
        return;   // voxel completely outside → skip rasterisation