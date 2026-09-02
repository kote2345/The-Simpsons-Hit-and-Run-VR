#version 450
#ifdef ENABLE_MULTIVIEW
#extension GL_EXT_multiview : enable
#endif

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;
layout(location = 3) in vec4 vertexColour;
layout(location = 4) in vec2 texcoord1;
layout(location = 5) in vec2 texcoord2;
layout(location = 6) in vec3 skinWeights;
layout(location = 7) in uvec4 skinIndices;
struct LightParams { vec4 position; vec4 colour; vec4 attenuation; };
layout(set = 1, binding = 0, std140) uniform DrawConstants
{
    mat4 modelview;
    vec4 materialColour;
    float alphaRef;
    int alphaCompare;
    vec4 ambientTerm;
    vec4 specularMaterial;
    vec4 fogColour;
    vec4 fogParams;
    vec4 environmentBlend;
    vec4 environmentParams;
    vec4 outputParams;
    vec4 materialParams;
    LightParams lights[8];
    mat4 reflectionViewToWorld;
    mat4 normalMatrix;
    vec4 skinParams;
    mat4 skinMatrices[25];
    mat4 shadowMatrices[3];
    vec4 shadowParams;
    vec4 vehicleRearLightPositions[4]; vec4 vehicleRearLightDirections[4];
    vec4 vehicleRearLightParams; vec4 vehicleRearLightControl; vec4 pbrMapControl;
} draw;
layout(push_constant) uniform TransformConstants { mat4 leftMvp; mat4 rightMvp; } transform;

layout(location = 0) out vec4 colour;
layout(location = 1) out vec2 uv;
layout(location = 2) out vec3 specularLight;
layout(location = 3) out float viewDepth;
layout(location = 4) out vec2 reflectionUV;
layout(location = 5) out vec2 uv1;
layout(location = 6) out vec2 uv2;
layout(location = 7) out vec4 shadowCoord0;
layout(location = 8) out vec4 shadowCoord1;
layout(location = 9) out vec4 shadowCoord2;
layout(location = 10) out vec4 rawColour;
layout(location = 11) out vec3 viewPositionOut;
layout(location = 12) out vec3 viewNormalOut;

void main()
{
    vec4 skinnedPosition=vec4(position,1.0);
    vec3 skinnedNormal=normal;
    if(draw.skinParams.x>0.5)
    {
        vec4 weights=vec4(skinWeights,1.0-skinWeights.x-skinWeights.y-skinWeights.z);
        skinnedPosition=vec4(0.0);
        skinnedNormal=vec3(0.0);
        for(int i=0;i<4;++i) {
            skinnedPosition+=draw.skinMatrices[skinIndices[i]]*vec4(position,1.0)*weights[i];
            skinnedNormal+=mat3(draw.skinMatrices[skinIndices[i]])*normal*weights[i];
        }
    }
#ifdef ENABLE_MULTIVIEW
    vec4 clip=transform.leftMvp*skinnedPosition;
    if(gl_ViewIndex==1)
        clip=transform.rightMvp*skinnedPosition;
#else
    vec4 clip = transform.leftMvp * skinnedPosition;
#endif
    if(int(draw.outputParams.y+0.5)==4) {
        vec4 displaced;
#ifdef ENABLE_MULTIVIEW
        displaced=transform.leftMvp*vec4(skinnedPosition.xyz+normalize(skinnedNormal)*0.025,1.0);
        if(gl_ViewIndex==1)
            displaced=transform.rightMvp*vec4(skinnedPosition.xyz+normalize(skinnedNormal)*0.025,1.0);
#else
        displaced=transform.leftMvp*vec4(skinnedPosition.xyz+normalize(skinnedNormal)*0.025,1.0);
#endif
        vec2 direction=displaced.xy/max(abs(displaced.w),0.0001)-
                       clip.xy/max(abs(clip.w),0.0001);
        float directionLength=length(direction);
        if(directionLength>0.00001)
            clip.xy+=direction/directionLength*clip.w*0.0032;
    }
    // Pure3D and the OpenXR projection builder use OpenGL's -W..+W clip
    // depth. Vulkan clips to 0..+W, so remap it before rasterization.
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;
    vec4 viewPosition=draw.modelview*skinnedPosition;
    shadowCoord0=draw.shadowMatrices[0]*viewPosition;
    shadowCoord1=draw.shadowMatrices[1]*viewPosition;
    shadowCoord2=draw.shadowMatrices[2]*viewPosition;
    viewDepth = length(viewPosition.xyz);
    vec3 reflectionNormal=vec3(0.0,0.0,1.0);
    reflectionUV=vec2(0.5);
    if(draw.materialParams.y > 0.5 || draw.environmentParams.x > 0.5)
        reflectionNormal=normalize(mat3(draw.normalMatrix)*skinnedNormal);
    if(draw.environmentParams.x > 0.5)
    {
        vec3 worldNormal=normalize(mat3(draw.reflectionViewToWorld)*reflectionNormal);
        vec3 worldIncident=normalize(mat3(draw.reflectionViewToWorld)*viewPosition.xyz);
        vec3 reflected=normalize(reflect(worldIncident,worldNormal));
        reflectionUV=clamp(vec2(0.5+0.5*reflected.x,0.5-0.5*reflected.y),
                           vec2(0.001),vec2(0.999));
    }
    vec3 diffuseLight=draw.ambientTerm.rgb;
    specularLight = vec3(0.0);
    if(draw.materialParams.y > 0.5)
    for(int i=0;i<8;++i)
    {
        if(draw.lights[i].attenuation.w < 0.5) continue;
        vec3 delta=draw.lights[i].position.xyz-
                   draw.lights[i].position.w*viewPosition.xyz;
        float distanceToLight=length(delta);
        vec3 L=delta/max(distanceToLight,0.0001);
        float ndl=max(dot(reflectionNormal,L),0.0);
        vec3 k=draw.lights[i].attenuation.xyz;
        float att=draw.lights[i].position.w!=0.0 ?
            1.0/max(k.x+k.y*distanceToLight+k.z*distanceToLight*distanceToLight,0.0001):1.0;
        diffuseLight+=att*ndl*draw.materialColour.rgb*draw.lights[i].colour.rgb;
        if(ndl>0.0)
        {
            vec3 H=normalize(L+vec3(0.0,0.0,1.0));
            float power=draw.materialParams.x!=0.0 ?
                pow(max(dot(reflectionNormal,H),0.0),draw.materialParams.x):1.0;
            specularLight+=att*power*draw.specularMaterial.rgb*draw.lights[i].colour.rgb;
        }
    }
    rawColour=vertexColour*vec4(1.0,1.0,1.0,draw.materialColour.a);
    viewPositionOut=viewPosition.xyz;
    viewNormalOut=reflectionNormal;
    colour=draw.outputParams.y>0.5&&draw.outputParams.z>0.5?
        rawColour:vertexColour*vec4(diffuseLight,draw.materialColour.a);
    // VulkanTexture::Lock mirrors the GLES negative-pitch contract, so the
    // authored Pure3D UVs pass through unchanged. Render targets have their
    // orientation corrected explicitly by their compositor geometry.
    uv = texcoord;
    uv1 = texcoord1;
    uv2 = texcoord2;
}
