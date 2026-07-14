#version 460 core

out vec4 FragColor;

layout(binding = 7) coherent buffer uInterleavedAccumulationBuffer {int accum[];};

uniform sampler2D uAccumulationTex; // was mAccumulationTexture (R32F, 2*width × height)
uniform sampler2D uDebugTex;        // was mFrameTexture        (RGBA8, width × height)
uniform bool uHdrEnabled;
uniform ivec2 uResolution;

vec3 CS_getStarColor(float temperature) { //REVEAL(temperature range clamped to [2300, 12000] deep blue stars get cut off, original cosmoscout has this limitation too)
    const float tMin = 2300.0;
    const float tMax = 12000.0; //TODO: allow color range from 1k to 40k since super-bright blue stars might not stand out otherwise

    const int  cSpectralColorsN    = 73;
    const vec3 cSpectralColors[73] = vec3[](
        vec3(1.0, 0.409, 0.078), vec3(1.0, 0.432, 0.093), vec3(1.0, 0.455, 0.109),
        vec3(1.0, 0.476, 0.126), vec3(1.0, 0.497, 0.144), vec3(1.0, 0.518, 0.163),
        vec3(1.0, 0.537, 0.182), vec3(1.0, 0.557, 0.202), vec3(1.0, 0.575, 0.223),
        vec3(1.0, 0.593, 0.244), vec3(1.0, 0.611, 0.266), vec3(1.0, 0.627, 0.289),
        vec3(1.0, 0.644, 0.311), vec3(1.0, 0.66,  0.335), vec3(1.0, 0.675, 0.358),
        vec3(1.0, 0.69,  0.382), vec3(1.0, 0.704, 0.405), vec3(1.0, 0.718, 0.429),
        vec3(1.0, 0.732, 0.454), vec3(1.0, 0.745, 0.478), vec3(1.0, 0.758, 0.502),
        vec3(1.0, 0.77,  0.527), vec3(1.0, 0.782, 0.551), vec3(1.0, 0.794, 0.575),
        vec3(1.0, 0.806, 0.599), vec3(1.0, 0.817, 0.624), vec3(1.0, 0.827, 0.648),
        vec3(1.0, 0.838, 0.672), vec3(1.0, 0.848, 0.696), vec3(1.0, 0.858, 0.719),
        vec3(1.0, 0.867, 0.743), vec3(1.0, 0.877, 0.766), vec3(1.0, 0.886, 0.789),
        vec3(1.0, 0.894, 0.812), vec3(1.0, 0.903, 0.835), vec3(1.0, 0.911, 0.858),
        vec3(1.0, 0.919, 0.88),  vec3(1.0, 0.927, 0.902), vec3(1.0, 0.935, 0.924),
        vec3(1.0, 0.942, 0.946), vec3(1.0, 0.95,  0.967), vec3(1.0, 0.957, 0.989),
        vec3(0.991, 0.955, 1.0), vec3(0.971, 0.942, 1.0), vec3(0.952, 0.93,  1.0),
        vec3(0.934, 0.918, 1.0), vec3(0.917, 0.907, 1.0), vec3(0.901, 0.896, 1.0),
        vec3(0.87,  0.876, 1.0), vec3(0.843, 0.858, 1.0), vec3(0.817, 0.841, 1.0),
        vec3(0.794, 0.825, 1.0), vec3(0.773, 0.81,  1.0), vec3(0.753, 0.797, 1.0),
        vec3(0.735, 0.784, 1.0), vec3(0.718, 0.772, 1.0), vec3(0.703, 0.761, 1.0),
        vec3(0.688, 0.75,  1.0), vec3(0.674, 0.741, 1.0), vec3(0.662, 0.731, 1.0),
        vec3(0.65,  0.723, 1.0), vec3(0.639, 0.714, 1.0), vec3(0.628, 0.706, 1.0),
        vec3(0.618, 0.699, 1.0), vec3(0.609, 0.692, 1.0), vec3(0.6,   0.685, 1.0),
        vec3(0.592, 0.679, 1.0), vec3(0.584, 0.673, 1.0), vec3(0.577, 0.667, 1.0),
        vec3(0.57,  0.662, 1.0), vec3(0.563, 0.657, 1.0), vec3(0.557, 0.652, 1.0),
        vec3(0.55,  0.647, 1.0));

    float t          = clamp((temperature - tMin) / (tMax - tMin), 0.0, 1.0);
    int   global_idx = int(t * float(cSpectralColorsN - 1));
    return cSpectralColors[global_idx];
}

// http://filmicworlds.com/blog/filmic-tonemapping-operators/
const float A = 0.15;
const float B = 0.50;
const float C = 0.10;
const float D = 0.20;
const float E = 0.02;
const float F = 0.30;
const float W = 11.2;

vec3 Uncharted2Tonemap(vec3 x) {
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (pixel.x > uResolution.x || pixel.y > uResolution.y) discard;

    // Pass through anything the rasterize pass already wrote (debug overlay etc.)
    vec4 debug = texelFetch(uDebugTex, pixel, 0);
    if (debug.r > 0.0) {
        FragColor = debug * 0.8;
        return;
    }

    float sumWeightedTemp = 0.0;
    float sumLumi = 0.0;

    for(uint lane = 0; lane < 2; lane++) { //REVEAL(lane workloading, lanes don't do so much, going too high worsens performace)
        uint fbOffsetFromLane = uResolution.y * lane;
        uint fbIndexOffset = uResolution.y * uResolution.x * lane * 2;

        /*float accumulatedWeightedTemperature =
            texelFetch(uAccumulationTex, pixel * ivec2(2,1) + ivec2(0,fbOffsetFromLane), 0).r;
        float accumulatedLuminance =
            texelFetch(uAccumulationTex, pixel * ivec2(2,1) + ivec2(1,fbOffsetFromLane), 0).r;
        */

        uint index_base = (uResolution.x * pixel.y + pixel.x) * 2;
        float accumulatedWeightedTemperature = accum[index_base + 0 + fbIndexOffset];
        float accumulatedLuminance = accum[index_base + 1 + fbIndexOffset];

        sumWeightedTemp += accumulatedWeightedTemperature;
        sumLumi += accumulatedLuminance;
    }

    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);
    if (sumLumi > 0.0 && sumWeightedTemp > 0.0) {
        float avgT = sumWeightedTemp / sumLumi;
        vec3 hdr = CS_getStarColor(avgT) * sumLumi / 1e6;
        //vec3 ldr = hdr / (1.0 + hdr);   // Reinhard
        color.rgb = hdr;          // matches the original (ldr was commented out)
    }
    //heuristic tonemap if no hdr, same as SRpoint renderer of cosmoscout
    if(!uHdrEnabled) color.rgb = Uncharted2Tonemap(color.rgb * 4e3);

    FragColor = color;
}