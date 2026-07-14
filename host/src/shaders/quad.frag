#version 460 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uColorTex;

void main() {
    vec4 inRGBA = texture(uColorTex, vUV);
    //uint packedColor = floatBitsToUint(inFloat);
    //vec4 color = unpackUnorm4x8(packedColor);
    //FragColor = color;
    FragColor = inRGBA;
}
