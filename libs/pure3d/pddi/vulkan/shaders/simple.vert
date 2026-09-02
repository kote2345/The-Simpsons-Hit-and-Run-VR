#version 450
#ifdef ENABLE_MULTIVIEW
#extension GL_EXT_multiview : enable
#endif
layout(location=0) in vec3 position;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec4 vertexColour;
layout(location=6) in vec3 skinWeights;
layout(location=7) in uvec4 skinIndices;
struct LightParams { vec4 position; vec4 colour; vec4 attenuation; };
layout(set=1,binding=0,std140) uniform DrawConstants {
    mat4 modelview; vec4 materialColour; float alphaRef; int alphaCompare;
    vec4 ambientTerm; vec4 specularMaterial; vec4 fogColour; vec4 fogParams;
    vec4 environmentBlend; vec4 environmentParams; vec4 outputParams; vec4 materialParams;
    LightParams lights[8]; mat4 reflectionViewToWorld; mat4 normalMatrix;
    vec4 skinParams; mat4 skinMatrices[25]; mat4 shadowMatrices[3]; vec4 shadowParams;
} draw;
layout(push_constant) uniform TransformConstants { mat4 leftMvp; mat4 rightMvp; } transform;
layout(location=0) out vec4 colour;
layout(location=1) out vec2 uv;
layout(location=3) out float viewDepth;
layout(location=7) out vec4 shadowCoord0;
layout(location=8) out vec4 shadowCoord1;
layout(location=9) out vec4 shadowCoord2;
void main() {
    vec4 skinnedPosition=vec4(position,1.0);
    if(draw.skinParams.x>0.5) {
        vec4 weights=vec4(skinWeights,1.0-skinWeights.x-skinWeights.y-skinWeights.z);
        skinnedPosition=vec4(0.0);
        for(int i=0;i<4;++i) skinnedPosition+=draw.skinMatrices[skinIndices[i]]*vec4(position,1.0)*weights[i];
    }
#ifdef ENABLE_MULTIVIEW
    vec4 clip=(gl_ViewIndex==0?transform.leftMvp:transform.rightMvp)*skinnedPosition;
#else
    vec4 clip=transform.leftMvp*skinnedPosition;
#endif
    clip.z=(clip.z+clip.w)*0.5;
    gl_Position=clip;
    vec4 viewPosition=draw.modelview*skinnedPosition;
    viewDepth=length(viewPosition.xyz);
    shadowCoord0=draw.shadowMatrices[0]*viewPosition;
    shadowCoord1=draw.shadowMatrices[1]*viewPosition;
    shadowCoord2=draw.shadowMatrices[2]*viewPosition;
    colour=vertexColour*vec4(draw.ambientTerm.rgb,draw.materialColour.a);
    uv=texcoord;
}
