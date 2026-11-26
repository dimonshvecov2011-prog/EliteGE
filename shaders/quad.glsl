#version 450

#ifdef VERTEX_SHADER
layout(binding = 0) uniform Camera { mat4 viewProjection; } camera;
layout(push_constant) uniform Object { mat4 model; } object;
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;

void main() {
    gl_Position = camera.viewProjection * object.model * vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
    fragUV = inUV;
}
#else
layout(binding = 1) uniform sampler2D spriteTexture;
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(spriteTexture, fragUV) * vec4(fragColor, 1.0);
}
#endif
