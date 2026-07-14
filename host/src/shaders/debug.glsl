
void DEBUG_fillPixelThick(ivec2 coord, vec4 color) {
    for(int x = 0; x < 3; x++) {
        for(int y = 0; y < 3; y++) {
            imageStore(uColorBuffer, coord + ivec2(x,y),
                color
            );
        }
    }
}

void DEBUG_drawLine(ivec2 start, ivec2 end, vec4 color) {
    vec2 x0 = start;
    vec2 x1 = end;
    float len = length(x1 - x0);
    vec2 dir = normalize(x1 - x0);
    for(int i = 0; i <= len; i++) {
        ivec2 x = ivec2(x0 + dir * float(i));
        if(x.x < 0 || x.y < 0 || x.x > uResolution.x - 1 || x.y > uResolution.y - 1) continue;
        imageStore(uColorBuffer, x, color);
    }
}

void DEBUG_drawBinaryStripes(uvec4 ci) {
    for(uint i = 0; i < 32; i++) {
        imageStore(uColorBuffer, ivec2(gl_GlobalInvocationID.x, 20 + i ),
            vec4(1.0) * float((ci.x >> i) & 1)
        );
        imageStore(uColorBuffer, ivec2(gl_GlobalInvocationID.x, 50 + i),
            vec4(1.0, 0.7, 0.0, 1.0) * float((ci.y >> i) & 1)
        );
        imageStore(uColorBuffer, ivec2(gl_GlobalInvocationID.x, 80 + i),
            vec4(1.0, 0.7, 0.0, 1.0) * float((ci.z >> i) & 1)
        );
    }
}

void DEBUG_drawIsometricMinimap(vec3 voxelCenter, uint voxelStarCount) {
     vec3 cc = isoMat * voxelCenter;

    vec4 cellColorDebug = vec4(voxelCenter.x / 32.0 + 0.5,
        voxelCenter.y / 8.0 + 0.5,
        voxelCenter.z / 32.0 + 0.5
        , 1.0) * 1.5;

    DEBUG_fillPixelThick( ivec2(cc.x * 6, cc.y * 6 + voxelCenter.y * 2) + ivec2(480, 420),
        cellColorDebug
    );
    
    vec3 ccCam = isoMat * uCameraPos;
    DEBUG_fillPixelThick( ivec2(ccCam.x * 6, ccCam.y * 6) + ivec2(480, 420),
        vec4(1,0,0,1)
    );

    for(int x = 0; x < log10(voxelStarCount); x++) {
        imageStore(uColorBuffer, ivec2(cc.x * 6 + x + 3, cc.y * 6 + voxelCenter.y * 2) + ivec2(480, 420),
            cellColorDebug
        );
    }
}

void DEBUG_drawDebugCellText(vec3 voxelCenter, uvec4 ci, vec4 voxelColor) {
    ivec2 text_pos = ivec2(
        (gl_GlobalInvocationID.x / 80) * 240,
        1000 - (gl_GlobalInvocationID.x % 80) * 8
    );
    
    drawInt(int(gl_GlobalInvocationID.x), text_pos, false, vec4(1.0));
    drawInt(int(ci.x), text_pos + ivec2(30, 0), true, voxelColor);
    drawInt(int(ci.y), text_pos + ivec2(70, 0), false, voxelColor);
    drawInt(int(ci.z), text_pos + ivec2(90, 0), false, voxelColor);
    drawInt(int(ci.w), text_pos + ivec2(120, 0), true, voxelColor);
    drawInt(int(voxelCenter.x), text_pos + ivec2(180, 0), false, voxelColor);
    drawInt(int(voxelCenter.y), text_pos + ivec2(200, 0), false, voxelColor);
    drawInt(int(voxelCenter.z), text_pos + ivec2(220, 0), false, voxelColor);
}


//  draw a wire‑frame outline of a voxel (axis‑aligned unit cube)
void DEBUG_drawVoxelOutline(vec3 cellPos, float cellSize, vec4 color)
{
    //build the corner points (world space)
    const vec3 h = vec3(cellSize * 0.5); //h = half
    cellPos += cellSize * 0.5;
    vec3 corner[4];

    corner[0] = cellPos + vec3(-h.x, -h.y, -h.z); // 000
    corner[1] = cellPos + vec3(+h.x, -h.y, -h.z); // 100
    corner[2] = cellPos + vec3(-h.x, +h.y, -h.z); // 010
    corner[3] = cellPos + vec3(-h.x, -h.y, +h.z); // 001

    //project corners to pixel coordinates
    ivec2 pixelCorner[4];
    for (int i = 0; i < 4; i++) pixelCorner[i] = toScreen(corner[i]);
    
    drawInt(int(corner[0].x * 1000.0), pixelCorner[0] + ivec2(0, 0), false, color);
    drawInt(int(corner[0].y * 1000.0), pixelCorner[0] + ivec2(0, 8), false, color);
    drawInt(int(corner[0].z * 1000.0), pixelCorner[0] + ivec2(0, 16), false, color);
    
    //define edges
    const int edgeIdx[3][2] = int[3][2](
        int[2](0,1), int[2](0,2), int[2](0,3)
    );

    //draw edges
    for (int e = 0; e < 3; e++)
    {
        const int a = edgeIdx[e][0];
        const int b = edgeIdx[e][1];
        
        ivec2 x0 = pixelCorner[a];
        ivec2 x1 = pixelCorner[b];
        
        //discard if toScreen tells that line goes outside screen
        //TODO: make this check pixel-wise
        if(x0.x < 0 || x1.x < 0) continue;
        
        drawLine(x0, x1, color);
    }
}