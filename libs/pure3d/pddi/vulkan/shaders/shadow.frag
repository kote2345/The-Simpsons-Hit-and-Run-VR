#version 450
layout(location=0) in vec2 uv;
layout(set=0,binding=0) uniform sampler2D diffuseTexture;
struct LightParams { vec4 position; vec4 colour; vec4 attenuation; };
layout(set=1,binding=0,std140) uniform DrawConstants {
    mat4 modelview; vec4 materialColour; float alphaRef; int alphaCompare;
    vec4 ambientTerm; vec4 specularMaterial; vec4 fogColour; vec4 fogParams;
    vec4 environmentBlend; vec4 environmentParams; vec4 outputParams; vec4 materialParams;
    LightParams lights[8]; mat4 reflectionViewToWorld; mat4 normalMatrix;
    vec4 skinParams; mat4 skinMatrices[25];
} draw;
void main() { if(draw.alphaRef>=0.0 && texture(diffuseTexture,uv).a<draw.alphaRef) discard; }
