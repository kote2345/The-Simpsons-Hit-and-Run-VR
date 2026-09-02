#version 450
layout(constant_id=0) const bool kAlphaTest=true;
layout(location=0) in vec4 colour;
layout(location=1) in vec2 uv;
layout(location=3) in float viewDepth;
layout(location=7) in vec4 shadowCoord0;
layout(location=8) in vec4 shadowCoord1;
layout(location=9) in vec4 shadowCoord2;
layout(location=0) out vec4 outputColour;
layout(set=0,binding=0) uniform sampler2D diffuseTexture;
layout(set=5,binding=0) uniform sampler2DShadow shadowTexture0;
layout(set=5,binding=1) uniform sampler2DShadow shadowTexture1;
layout(set=5,binding=2) uniform sampler2DShadow shadowTexture2;
struct LightParams { vec4 position; vec4 colour; vec4 attenuation; };
layout(set=1,binding=0,std140) uniform DrawConstants {
    mat4 modelview; vec4 materialColour; float alphaRef; int alphaCompare;
    vec4 ambientTerm; vec4 specularMaterial; vec4 fogColour; vec4 fogParams;
    vec4 environmentBlend; vec4 environmentParams; vec4 outputParams; vec4 materialParams;
    LightParams lights[8]; mat4 reflectionViewToWorld; mat4 normalMatrix;
    vec4 skinParams; mat4 skinMatrices[25]; mat4 shadowMatrices[3]; vec4 shadowParams;
} draw;
vec3 shadowPosition(vec4 coordinate) {
    vec3 p=coordinate.xyz/coordinate.w; p.y=-p.y; p=p*0.5+0.5;
    return p;
}
bool validShadowPosition(vec3 p) {
    return p.x>0.001&&p.x<0.999&&p.y>0.001&&p.y<0.999&&p.z>0.0&&p.z<1.0;
}
float sampleShadow0(vec4 coordinate) {
    vec3 p=shadowPosition(coordinate);
    if(!validShadowPosition(p)) return 0.0;
    return texture(shadowTexture0,vec3(p.xy,p.z-draw.shadowParams.w));
}
float sampleShadow1(vec4 coordinate) {
    vec3 p=shadowPosition(coordinate);
    if(!validShadowPosition(p)) return 0.0;
    return texture(shadowTexture1,vec3(p.xy,p.z-draw.shadowParams.w));
}
float sampleShadow2(vec4 coordinate) {
    vec3 p=shadowPosition(coordinate);
    if(!validShadowPosition(p)) return 0.0;
    return texture(shadowTexture2,vec3(p.xy,p.z-draw.shadowParams.w));
}
float csmShadow() {
    if(viewDepth<20.0) return sampleShadow0(shadowCoord0);
    if(viewDepth<24.0) {
        float s0=sampleShadow0(shadowCoord0);
        float s1=sampleShadow1(shadowCoord1);
        float nearShadow=max(s0,s1);
        return mix(nearShadow,s1,(viewDepth-20.0)*0.25);
    }
    if(viewDepth<50.0) return sampleShadow1(shadowCoord1);
    if(viewDepth<56.0) {
        float s1=sampleShadow1(shadowCoord1);
        float s2=sampleShadow2(shadowCoord2);
        return mix(s1,s2,(viewDepth-50.0)/6.0);
    }
    return sampleShadow2(shadowCoord2);
}
void main() {
    outputColour=colour*texture(diffuseTexture,uv);
    if(draw.shadowParams.x>0.5) {
        outputColour.rgb*=1.0-0.435*csmShadow();
    }
    if(kAlphaTest&&draw.alphaRef>=0.0) {
        bool pass=draw.alphaCompare==1 ||
            (draw.alphaCompare==2&&outputColour.a<draw.alphaRef) ||
            (draw.alphaCompare==3&&outputColour.a<=draw.alphaRef) ||
            (draw.alphaCompare==4&&outputColour.a>draw.alphaRef) ||
            (draw.alphaCompare==5&&outputColour.a>=draw.alphaRef) ||
            (draw.alphaCompare==6&&outputColour.a==draw.alphaRef) ||
            (draw.alphaCompare==7&&outputColour.a!=draw.alphaRef);
        if(!pass) discard;
    }
}
