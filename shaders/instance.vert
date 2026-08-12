#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inOffset;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inSize;

layout(location = 0) out vec4 vColor;

layout(set = 0, binding = 0) uniform MVP {
    mat4 viewProj;
} ubo;

void main() {
    vec3 world = vec3(inOffset.x + inPos.x * inSize, 0.0, inOffset.y + inPos.y * inSize);
    gl_Position = ubo.viewProj * vec4(world, 1.0);
    vColor = inColor;
}
