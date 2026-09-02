#version 450
#ifdef ENABLE_MULTIVIEW
#extension GL_EXT_multiview : enable
#endif
layout(location=0) in vec3 position;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec4 vertexColour;
layout(set=1,binding=0,std140) uniform CompactDraw {
    vec4 colour;
    vec4 alpha;
} draw;
layout(push_constant) uniform TransformConstants {
    mat4 leftMvp;
    mat4 rightMvp;
} transform;
layout(location=0) out vec4 colour;
layout(location=1) out vec2 uv;
void main() {
#ifdef ENABLE_MULTIVIEW
    vec4 clip=(gl_ViewIndex==0?transform.leftMvp:transform.rightMvp)*vec4(position,1.0);
#else
    vec4 clip=transform.leftMvp*vec4(position,1.0);
#endif
    clip.z=(clip.z+clip.w)*0.5;
    gl_Position=clip;
    colour=vertexColour*draw.colour;
    uv=texcoord;
}
