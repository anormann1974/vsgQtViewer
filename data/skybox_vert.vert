#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 modelView;
} pc;

layout(location = 0) in vec3 osg_Vertex;
layout(location = 0) out vec3 outUVW;

out gl_PerVertex{ vec4 gl_Position; };

void main()
{
    outUVW = osg_Vertex;
    outUVW.xy *= -1.0;

    gl_Position = pc.projection * vec4(osg_Vertex, 1.0);
}
