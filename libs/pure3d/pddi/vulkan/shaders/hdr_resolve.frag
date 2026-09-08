#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=1) flat in int eye;
layout(location=0) out vec4 outputColour;
layout(set=0,binding=0) uniform sampler2DArray scene;
layout(set=0,binding=1) uniform sampler2DArray depth;
layout(std430,set=0,binding=2) readonly buffer ExposureState { vec4 value[20]; } exposureState;
layout(set=0,binding=7) uniform sampler2DArray froxelVolume;
layout(push_constant) uniform Camera { mat4 projection; mat4 inverseProjection; } camera;
layout(constant_id=0) const bool outputSrgb=false;
layout(constant_id=3) const bool volumeOnly=false;
#include "volumetric_sampling.glsl"
vec3 positionAt(vec2 p) {
    float z=textureLod(depth,vec3(p,eye),0.0).r;
    // Geometry flips Y with a negative viewport and remaps GL clip Z to Vulkan.
    vec4 h=camera.inverseProjection*vec4(p.x*2.0-1.0,1.0-p.y*2.0,z*2.0-1.0,1.0);
    float divisor=abs(h.w)<1e-6?(h.w<0.0?-1e-6:1e-6):h.w;
    return h.xyz/divisor;
}
void main() {
    vec4 hdr=textureLod(scene,vec3(uv,eye),0.0);
    bool volumeEnabled=exposureState.value[14].y>0.5;
    float sceneDepth=volumeEnabled?textureLod(depth,vec3(uv,eye),0.0).r:1.0;
    vec4 volume=volumeEnabled?sampleVolumeAtDepth(uv,eye,sceneDepth):vec4(0.0,0.0,0.0,1.0);
    // On background pixels the HDR scene sample is the actual rendered
    // skybox. Preserve its chroma in the aerial perspective instead of
    // imposing a constant grey/gold fog colour.
    float skyMask=smoothstep(0.9985,1.0,sceneDepth);
    float skyLuminance=max(dot(hdr.rgb,vec3(0.2126,0.7152,0.0722)),0.0001);
    vec3 skyChroma=clamp(hdr.rgb/skyLuminance,vec3(0.35),vec3(2.5));
    volume.rgb*=mix(vec3(1.0),skyChroma,skyMask*0.72);
    float exposure=clamp(exposureState.value[eye].x,0.32,2.40);
    vec3 composite=hdr.rgb*clamp(volume.a,0.0,1.0)+volume.rgb;
    vec3 radiance=max(volumeOnly?volume.rgb*1.6:composite,vec3(0.0))*exposure;
    radiance=min(radiance,vec3(65504.0));
    vec3 mapped=clamp((radiance*(2.51*radiance+0.03))/(radiance*(2.43*radiance+0.59)+0.14),0.0,1.0);
    if(!outputSrgb) mapped=mix(1.055*pow(mapped,vec3(1.0/2.4))-0.055,
                               mapped*12.92,lessThanEqual(mapped,vec3(0.0031308)));
    outputColour=vec4(mapped,hdr.a);
}
