#version 450
#extension GL_GOOGLE_include_directive : require
#define MATERIAL_MODEL 2
layout(constant_id=0) const bool kAlphaTest=true;
layout(constant_id=1) const bool kHdrScene=false;

layout(location = 0) in vec4 colour;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 specularLight;
layout(location = 3) in float viewDepth;
layout(location = 4) in vec2 reflectionUV;
layout(location = 5) in vec2 uv1;
layout(location = 6) in vec2 uv2;
layout(location = 7) in vec4 shadowCoord0;
layout(location = 8) in vec4 shadowCoord1;
layout(location = 9) in vec4 shadowCoord2;
layout(location = 10) in vec4 rawColour;
layout(location = 11) in vec3 viewPositionOut;
layout(location = 12) in vec3 viewNormalOut;
layout(location = 0) out vec4 outputColour;
layout(set = 0, binding = 0) uniform sampler2D diffuseTexture;
layout(set = 2, binding = 0) uniform sampler2D reflectionTexture;
layout(set = 3, binding = 0) uniform sampler2D topTexture;
layout(set = 4, binding = 0) uniform sampler2D lightMapTexture;
layout(set = 5, binding = 0) uniform sampler2DShadow shadowTexture0;
layout(set = 5, binding = 1) uniform sampler2DShadow shadowTexture1;
layout(set = 5, binding = 2) uniform sampler2DShadow shadowTexture2;
layout(set = 6, binding = 0) uniform sampler2D pbrTextures;
layout(set = 5, binding = 3) uniform samplerCube dynamicReflectionTexture;
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
#include "pbr_shadows.glsl"
const float PI=3.14159265359;
bool hasPbrFlag(float bit) { return mod(floor(draw.pbrMapControl.x/bit),2.0)>0.5; }
vec3 srgbToLinear(vec3 value) {
    bvec3 cutoff=lessThanEqual(value,vec3(0.04045));
    vec3 low=value/12.92;
    vec3 high=pow((value+0.055)/1.055,vec3(2.4));
    return mix(high,low,cutoff);
}
vec3 linearToSrgb(vec3 value) {
    value=max(value,vec3(0.0));
    bvec3 cutoff=lessThanEqual(value,vec3(0.0031308));
    vec3 low=value*12.92;
    vec3 high=1.055*pow(value,vec3(1.0/2.4))-0.055;
    return mix(high,low,cutoff);
}
vec3 filmicToneMap(vec3 value) {
    return clamp((value*(2.51*value+0.03))/(value*(2.43*value+0.59)+0.14),0.0,1.0);
}
#include "pbr_brdf.glsl"
#include "pbr_punctual.glsl"
mat3 cotangentFrame(vec3 N,vec3 position,vec2 texcoord) {
    vec3 dp1=dFdx(position),dp2=dFdy(position);
    vec2 duv1=dFdx(texcoord),duv2=dFdy(texcoord);
    vec3 dp2perp=cross(dp2,N),dp1perp=cross(N,dp1);
    vec3 T=dp2perp*duv1.x+dp1perp*duv2.x;
    vec3 B=dp2perp*duv1.y+dp1perp*duv2.y;
    float scale=inversesqrt(max(max(dot(T,T),dot(B,B)),0.000001));
    return mat3(T*scale,B*scale,N);
}
vec3 enhancedLighting(vec3 albedo) {
    int model=MATERIAL_MODEL,profile=int(draw.outputParams.z+0.5);
    if(model==0||profile==0) return albedo;
    if(model==4) return albedo*0.035;
    vec3 N=normalize(viewNormalOut),V=normalize(-viewPositionOut);
    // Keep the macro normal continuous across independently exported road
    // chunks.  Reflections are much more sensitive to their slightly
    // different averaged vertex normals than diffuse lighting is.
    vec3 geometricNormal=normalize(cross(dFdx(viewPositionOut),dFdy(viewPositionOut)));
    if(dot(geometricNormal,N)<0.0) geometricNormal=-geometricNormal;
    vec3 worldVertexNormal=normalize(mat3(draw.reflectionViewToWorld)*N);
    if(model==2&&profile==1&&worldVertexNormal.y>0.65)
        N=geometricNormal;
    if(model==2&&hasPbrFlag(1.0)) {
        vec2 normalXY=texture(pbrTextures,uv).rg*2.0-1.0;
        normalXY.y=-normalXY.y;
        vec3 mapNormal=normalize(vec3(normalXY,
            sqrt(max(1.0-dot(normalXY,normalXY),0.0001))));
        N=normalize(cotangentFrame(N,viewPositionOut,uv)*mapNormal);
    }
    if(model==1&&profile==1&&draw.materialParams.y<0.5) return albedo;
    vec3 base=albedo;
    if(draw.materialParams.y>0.5) {
        vec3 diffuseLight=draw.ambientTerm.rgb,localSpecular=vec3(0.0);
        for(int i=0;i<8;++i) {
            if(draw.lights[i].attenuation.w<0.5) continue;
            vec3 delta=draw.lights[i].position.xyz-draw.lights[i].position.w*viewPositionOut;
            float distanceToLight=length(delta); vec3 localL=delta/max(distanceToLight,0.001);
            float localNdl=max(dot(N,localL),0.0); vec3 attenuation=draw.lights[i].attenuation.xyz;
            float att=draw.lights[i].position.w!=0.0?1.0/max(attenuation.x+attenuation.y*distanceToLight+attenuation.z*distanceToLight*distanceToLight,0.0001):1.0;
            diffuseLight+=att*localNdl*draw.materialColour.rgb*draw.lights[i].colour.rgb;
            if(localNdl>0.0) {
                vec3 localH=normalize(localL+V);
                localSpecular+=att*pow(max(dot(N,localH),0.0),max(draw.materialParams.x,0.0001))*draw.specularMaterial.rgb*draw.lights[i].colour.rgb;
            }
        }
        base=albedo*diffuseLight+localSpecular;
    }
    vec3 L=normalize(draw.skinParams.yzw),H=normalize(L+V);
    float ndl=max(dot(N,L),0.0);
    float car=(profile==2||profile>=6)?1.0:0.0;
    float chr=profile==3?1.0:0.0;
    float rough=profile==4?1.0:0.0;
    float metal=profile==5?1.0:0.0;
    if(model==3) {
        float rawNdl=dot(N,L);
        float edge=max(fwidth(rawNdl)*0.65,0.035);
        float lightBand=smoothstep(-0.08-edge,0.02+edge,rawNdl);
        float csm=draw.shadowParams.x>0.5?csmShadow():0.0;
        float shadowBand=smoothstep(0.82,0.97,csm);
        float illumination=lightBand*(1.0-shadowBand);
        vec3 toonDiffuse=albedo*mix(vec3(0.64,0.66,0.70),
                                    vec3(1.04,1.02,0.96),illumination);
        float nh=max(dot(N,H),0.0);
        float toonSpec=smoothstep(0.72,0.82,pow(nh,12.0)*illumination);
        float specScale=(profile==2||profile>=6)?0.16:0.0;
        return clamp(toonDiffuse+vec3(toonSpec*specScale),0.0,1.0);
    }
    float specularStrength=0.055+0.32*car+0.055*chr-0.035*rough+0.41*metal;
    float specularPower=12.0+26.0*car+10.0*chr-3.0*rough+36.0*metal;
    float sunSpec=ndl>0.0001?pow(max(dot(N,H),0.0),specularPower)*specularStrength:0.0;
    float fresnelBase=1.0-clamp(dot(N,V),0.0,1.0);
    float fresnel=fresnelBase*fresnelBase*fresnelBase*fresnelBase*fresnelBase;
    float reflectionStrength=0.10*car+0.24*metal;
    if(model==1)
    {
        return clamp(base*(0.92+0.16*ndl)+vec3(sunSpec)+
                     vec3(0.16,0.20,0.28)*fresnel*reflectionStrength,0.0,1.0);
    }

    vec3 pbrAlbedo=hasPbrFlag(8.0)?clamp(albedo,vec3(0.0),vec3(1.0)):
                                      srgbToLinear(clamp(albedo,vec3(0.0),vec3(1.0)));
    float metallic=metal;
    // Use a matte default for legacy materials. Custom _rough data overrides
    // this constant below and therefore keeps its authored response.
    float roughness=clamp(0.65+0.25*rough-0.20*metal,0.05,1.0);
    vec4 authoredPbr=texture(pbrTextures,uv);
    if(hasPbrFlag(2.0)) roughness=clamp(authoredPbr.b,0.089,1.0);
    if(hasPbrFlag(4.0)) metallic=authoredPbr.a;
    vec3 normalDx=dFdx(N),normalDy=dFdy(N);
    float normalVariance=0.15*(dot(normalDx,normalDx)+dot(normalDy,normalDy));
    float kernelRoughness=min(2.0*normalVariance,0.20);
    roughness=clamp(sqrt(roughness*roughness+kernelRoughness),0.089,1.0);
    float authoredAo=1.0;
    float ndv=clamp(dot(N,V),0.001,1.0);
    vec3 F0=mix(vec3(0.04),pbrAlbedo,metallic);
    vec2 dfg=environmentBrdfApproximation(roughness,ndv);
    vec3 environmentReflectance=pbrEnvironmentReflectance(F0,dfg);
    vec3 worldNormal=normalize(mat3(draw.reflectionViewToWorld)*N);
    vec3 worldIncident=normalize(mat3(draw.reflectionViewToWorld)*viewPositionOut);
    vec3 reflectionDirection=normalize(reflect(worldIncident,worldNormal));
    float skyWeight=clamp(worldNormal.y*0.5+0.5,0.0,1.0);
    vec3 irradiance=mix(vec3(0.20,0.21,0.23),vec3(0.32,0.35,0.40),skyWeight);
    vec3 ambient=pbrAlbedo*irradiance*(1.0-metallic)*
        (1.0-environmentReflectance)*authoredAo;
    float reflectedSky=clamp(reflectionDirection.y*0.5+0.5,0.0,1.0);
    vec3 environmentLinear=mix(vec3(0.035,0.030,0.025),
                               vec3(0.42,0.50,0.64),reflectedSky);
    if(draw.environmentParams.x>0.5) {
        vec3 reflectionSample;
        if(draw.vehicleRearLightControl.y>0.5) {
            float maximumLod=float(max(textureQueryLevels(dynamicReflectionTexture)-1,0));
            reflectionSample=textureLod(dynamicReflectionTexture,reflectionDirection,
                                        roughness*maximumLod).rgb;
        } else {
            float maximumLod=float(max(textureQueryLevels(reflectionTexture)-1,0));
            vec2 mappedReflectionUV=clamp(vec2(0.5+0.5*reflectionDirection.x,
                0.5-0.5*reflectionDirection.y),vec2(0.001),vec2(0.999));
            reflectionSample=textureLod(reflectionTexture,mappedReflectionUV,
                                        roughness*maximumLod).rgb;
        }
        // Dark captures are valid occluded environments, not missing data.
        environmentLinear=srgbToLinear(max(reflectionSample,vec3(0.0)));
    }
    ambient+=environmentLinear*environmentReflectance*authoredAo;
    const vec3 sunRadiance=vec3(2.15,2.04,1.86);
    float shadowAmount=draw.shadowParams.x>0.5?csmShadow():0.0;
    // Fully blocked sunlight must also remove its GGX highlight. Do not add
    // a direct-light visibility floor; ambient/IBL remains separate.
    float sunVisibility=1.0-clamp(shadowAmount,0.0,1.0);
    vec3 direct=pbrDirectBrdf(N,V,L,pbrAlbedo,metallic,roughness,0.00465)*
        sunRadiance*sunVisibility;

    // PCVR punctual lights are stored in world space by vkdevice. Evaluate in
    // world space as well so head rotation cannot move the lighting.
    // The legacy directional light is represented by the CSM sun above.
    if(draw.pbrMapControl.z>0.5) {
        vec3 worldPosition=(draw.reflectionViewToWorld*vec4(viewPositionOut,1.0)).xyz;
        vec3 worldV=normalize(mat3(draw.reflectionViewToWorld)*V);
        for(int i=0;i<8;++i) {
            if(draw.lights[i].attenuation.w<0.5 || draw.lights[i].position.w<0.5) continue;
            vec3 delta=draw.lights[i].position.xyz-worldPosition;
            float distanceSquared=dot(delta,delta);
            float distanceToLight=sqrt(max(distanceSquared,0.000001));
            vec3 localL=delta/distanceToLight;
            vec3 brdf=punctualBrdf(worldNormal,worldV,localL,pbrAlbedo,metallic,roughness);
            vec3 k=max(draw.lights[i].attenuation.xyz,vec3(0.0));
            // Retain authored attenuation; cap the singularity at the lamp.
            float attenuation=1.0/max(k.x+k.y*distanceToLight+k.z*distanceSquared,1.0);
            direct+=brdf*srgbToLinear(clamp(draw.lights[i].colour.rgb,0.0,1.0))*
                attenuation*PI;
        }
        direct+=pbrRearLights(N,V,pbrAlbedo,metallic,roughness);
    }
    return ambient+direct;
}

vec4 combineLayer(vec4 base, vec4 top, int mode)
{
    if(mode == 0) return base;
    if(mode == 1) return mix(base, top, top.a);
    if(mode == 2) return base + top;
    if(mode == 3) return base - top;
    if(mode == 4) return base * top;
    if(mode == 5) return min(base * top * 2.0, vec4(1.0));
    // D3DTOP_MODULATEALPHA_ADDCOLOR with CURRENT as Arg1 and TEXTURE as
    // Arg2: Current.rgb + Current.a * Texture.rgb. This is not symmetric.
    if(mode == 6) return vec4(base.rgb + top.rgb * base.a, base.a * top.a);
    if(mode == 7) return vec4(base.rgb - top.rgb, top.a);
    return base * top;
}

vec3 vehicleRearLightContribution() {
    int mode=int(draw.vehicleRearLightParams.w+0.5);
    int count=int(draw.vehicleRearLightControl.x+0.5);
    if(mode==0||count==0) return vec3(0.0);
    vec3 N=normalize(viewNormalOut),add=vec3(0.0);
    for(int i=0;i<4;++i) {
        if(i>=count) break;
        vec3 fromLamp=viewPositionOut-draw.vehicleRearLightPositions[i].xyz;
        float d2=dot(fromLamp,fromLamp),radius=mode==1?5.0:7.0;
        if(d2>=radius*radius) continue;
        float d=sqrt(d2); vec3 ray=fromLamp/max(d,0.001);
        float coneDot=dot(ray,draw.vehicleRearLightDirections[i].xyz);
        if(coneDot<=0.48) continue;
        float cone=smoothstep(0.48,0.78,coneDot),fall=1.0-d/radius;
        float nearFade=smoothstep(0.30,0.85,d);
        float facing=0.12+0.88*max(dot(N,-ray),0.0);
        float road=1.0+0.70*max(N.y,0.0);
        add+=draw.vehicleRearLightParams.rgb*fall*fall*nearFade*cone*facing*road*0.925*
             (mode==1?0.736:0.624);
    }
    return add;
}
void main() {
    vec4 baseSample=texture(diffuseTexture,uv);
    int materialMode=int(draw.environmentParams.y+0.5);
    int layerBlend=int(draw.environmentParams.z+0.5);
    if(materialMode == 1 || materialMode == 3)
    {
        vec4 first=draw.environmentParams.w > 0.5 ? baseSample : colour*baseSample;
        outputColour=combineLayer(first,texture(topTexture,uv1),layerBlend);
        if(draw.environmentParams.w > 0.5) outputColour*=colour;
    }
    else outputColour=colour*baseSample;
    if(materialMode == 2)
        outputColour.rgb*=texture(lightMapTexture,uv1).rgb*2.0;
    else if(materialMode == 3)
        outputColour.rgb*=texture(lightMapTexture,uv2).rgb*2.0;
    int enhancedModel=MATERIAL_MODEL;
    bool pbr=enhancedModel==2;
    int pbrDebug=int(draw.pbrMapControl.y+0.5);
    if(pbr&&pbrDebug>0) {
        vec4 mapSample=texture(pbrTextures,uv);
        if(pbrDebug==1) {
            vec2 xy=mapSample.rg*2.0-1.0; xy.y=-xy.y;
            outputColour.rgb=vec3(xy*0.5+0.5,
                sqrt(max(1.0-dot(xy,xy),0.0)));
        } else if(pbrDebug==2) outputColour.rgb=vec3(mapSample.b);
        else if(pbrDebug==3) outputColour.rgb=vec3(mapSample.a);
        else outputColour.rgb=mapSample.rgb;
        if(kAlphaTest && draw.alphaRef >= 0.0 && outputColour.a<draw.alphaRef) discard;
        return;
    }
    if(draw.outputParams.y>0.5)
        outputColour.rgb=enhancedLighting(outputColour.rgb);
    else outputColour.rgb += specularLight;
    if(draw.shadowParams.x>0.5&&enhancedModel!=2&&enhancedModel!=3) {
        outputColour.rgb*=1.0-0.435*csmShadow();
    }

    // PCVR PBR already composes material-aware lamps before tone mapping.
    // Preserve the legacy lamp path for Quest and non-enhanced profiles.
    bool pbrLamps=pbr && draw.pbrMapControl.z>0.5 &&
                  draw.outputParams.y>0.5 && draw.outputParams.z>0.5;
    vec3 rearLight=pbrLamps?vec3(0.0):vehicleRearLightContribution();
    if(pbr) outputColour.rgb+=rearLight;
    else outputColour.rgb=clamp(outputColour.rgb+rearLight,0.0,1.0);
    // PBR evaluates its environment inside enhancedLighting for both mapped
    // and fallback materials. Never add the old EnvMap pass on top merely
    // because a material has no authored PBR texture.
    if(draw.environmentParams.x > 0.5 &&
       int(draw.outputParams.y+0.5)!=2)
    {
        vec3 reflectionNormal=normalize(viewNormalOut);
        vec3 worldNormal=normalize(mat3(draw.reflectionViewToWorld)*reflectionNormal);
        vec3 worldIncident=normalize(mat3(draw.reflectionViewToWorld)*viewPositionOut);
        vec3 reflectionDirection=normalize(reflect(worldIncident,worldNormal));
        vec3 reflectionSample=draw.vehicleRearLightControl.y>0.5?
            texture(dynamicReflectionTexture,reflectionDirection).rgb:
            texture(reflectionTexture,reflectionUV).rgb;
        vec3 environment=pow(max(reflectionSample,vec3(0.0)),vec3(0.78));
        if(draw.environmentBlend.a < 0.5)
        {
            // Traffic textures encode recolourable bodywork in alpha. Keep
            // opaque trim out of the authored sphere-map reflection.
            float bodyMask=1.0-step(250.0/255.0,baseSample.a);
            outputColour.rgb=clamp(outputColour.rgb+
                environment*draw.environmentBlend.r*bodyMask,0.0,1.0);
        }
        else
        {
            outputColour.rgb=clamp(outputColour.rgb+
                environment*draw.environmentBlend.rgb*0.72,0.0,1.0);
        }
    }
    if(draw.fogParams.x > 0.5)
    {
        float fogRange=max(draw.fogParams.z-draw.fogParams.y,0.0001);
        float fogAmount=clamp((viewDepth-draw.fogParams.y)/fogRange,0.0,1.0);
        vec3 fog=pbr?srgbToLinear(draw.fogColour.rgb):draw.fogColour.rgb;
        outputColour.rgb=mix(outputColour.rgb,fog,fogAmount);
    }
    if(pbr && !kHdrScene) outputColour.rgb=linearToSrgb(filmicToneMap(outputColour.rgb));
    if(kAlphaTest && draw.alphaRef >= 0.0)
    {
        bool pass = draw.alphaCompare == 1 ||
                    (draw.alphaCompare == 2 && outputColour.a <  draw.alphaRef) ||
                    (draw.alphaCompare == 3 && outputColour.a <= draw.alphaRef) ||
                    (draw.alphaCompare == 4 && outputColour.a >  draw.alphaRef) ||
                    (draw.alphaCompare == 5 && outputColour.a >= draw.alphaRef) ||
                    (draw.alphaCompare == 6 && outputColour.a == draw.alphaRef) ||
                    (draw.alphaCompare == 7 && outputColour.a != draw.alphaRef);
        if(!pass) discard;
    }
}
