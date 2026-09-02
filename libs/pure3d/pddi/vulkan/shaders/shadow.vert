#version 450
layout(location=0) in vec3 position;
layout(location=2) in vec2 texcoord;
layout(location=6) in vec3 skinWeights;
layout(location=7) in uvec4 skinIndices;
struct LightParams { vec4 position; vec4 colour; vec4 attenuation; };
layout(set=1,binding=0,std140) uniform DrawConstants {
    mat4 modelview; vec4 materialColour; float alphaRef; int alphaCompare;
    vec4 ambientTerm; vec4 specularMaterial; vec4 fogColour; vec4 fogParams;
    vec4 environmentBlend; vec4 environmentParams; vec4 outputParams; vec4 materialParams;
    LightParams lights[8]; mat4 reflectionViewToWorld; mat4 normalMatrix;
    vec4 skinParams; mat4 skinMatrices[25];
} draw;
layout(push_constant) uniform TransformConstants { mat4 mvp; } transform;
layout(location=0) out vec2 uv;
void main() {
    vec4 p=vec4(position,1.0);
    if(draw.skinParams.x>0.5) {
        vec4 w=vec4(skinWeights,1.0-skinWeights.x-skinWeights.y-skinWeights.z);
        p=vec4(0.0);
        for(int i=0;i<4;++i) p+=draw.skinMatrices[skinIndices[i]]*vec4(position,1.0)*w[i];
    }
    vec4 clip=transform.mvp*p;
    clip.z=(clip.z+clip.w)*0.5;
    gl_Position=clip;
    uv=texcoord;
}
