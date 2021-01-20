#version 450
#extension GL_ARB_separate_shader_objects : enable

//layout(binding = 0) uniform sampler2D diffuseMap;
layout(location = 0) in vec3 inUVW;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(0.8, 0.0, 0.0, 1.0);
}

