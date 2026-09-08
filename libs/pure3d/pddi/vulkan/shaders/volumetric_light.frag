#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=1) flat in int eye;
layout(location=0) out vec4 outputColour;
layout(set=0,binding=1) uniform sampler2DArray depth;
layout(set=0,binding=7) uniform sampler2DArray froxelVolume;
layout(push_constant) uniform Camera { mat4 projection; mat4 inverseProjection; } camera;
#include "volumetric_sampling.glsl"
void main() { outputColour=sampleVolume(uv,eye); }
