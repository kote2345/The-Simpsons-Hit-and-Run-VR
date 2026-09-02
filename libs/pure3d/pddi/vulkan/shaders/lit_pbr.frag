#version 450
#define MATERIAL_MODEL 2
layout(constant_id=0) const bool kAlphaTest=true;
layout(location=0) in vec4 colour;
layout(location=1) in vec2 uv;
layout(location=2) in vec3 specularLight;
layout(location=3) in float viewDepth;
layout(location=7) in vec4 shadowCoord0;
layout(location=8) in vec4 shadowCoord1;
layout(location=9) in vec4 shadowCoord2;
layout(location=10) in vec4 rawColour;
layout(location=11) in vec3 viewPositionOut;
layout(location=12) in vec3 viewNormalOut;
layout(location=0) out vec4 outputColour;
layout(set=0,binding=0) uniform sampler2D diffuseTexture;
layout(set=2,binding=0) uniform sampler2D reflectionTexture;
layout(set=5,binding=0) uniform sampler2DShadow shadowTexture0;
layout(set=5,binding=1) uniform sampler2DShadow shadowTexture1;
layout(set=5,binding=2) uniform sampler2DShadow shadowTexture2;
layout(set=6,binding=0) uniform sampler2D pbrTextures;
layout(set=5,binding=3) uniform samplerCube dynamicReflectionTexture;
struct LightParams { vec4 position; vec4 colour; vec4 attenuation; };
layout(set=1,binding=0,std140) uniform DrawConstants {
    mat4 modelview; vec4 materialColour; float alphaRef; int alphaCompare;
    vec4 ambientTerm; vec4 specularMaterial; vec4 fogColour; vec4 fogParams;
    vec4 environmentBlend; vec4 environmentParams; vec4 outputParams; vec4 materialParams;
    LightParams lights[8]; mat4 reflectionViewToWorld; mat4 normalMatrix;
    vec4 skinParams; mat4 skinMatrices[25]; mat4 shadowMatrices[3]; vec4 shadowParams;
    vec4 vehicleRearLightPositions[4]; vec4 vehicleRearLightDirections[4];
    vec4 vehicleRearLightParams; vec4 vehicleRearLightControl; vec4 pbrMapControl;
} draw;
vec3 shadowPosition(vec4 coordinate) {
    vec3 p=coordinate.xyz/coordinate.w; p.y=-p.y; p=p*0.5+0.5;
    return p;
}
bool validShadowPosition(vec3 p) {
    return p.x>0.001&&p.x<0.999&&p.y>0.001&&p.y<0.999&&p.z>0.0&&p.z<1.0;
}
// Keep each opaque sampler statically tied to its descriptor binding.  Some
// mobile Vulkan drivers miscompile a sampler2DShadow passed as a function
// argument, causing all calls to keep sampling the first descriptor.
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
bool alphaPass(float a) {
    return draw.alphaCompare==1 || (draw.alphaCompare==2&&a<draw.alphaRef) ||
        (draw.alphaCompare==3&&a<=draw.alphaRef) || (draw.alphaCompare==4&&a>draw.alphaRef) ||
        (draw.alphaCompare==5&&a>=draw.alphaRef) || (draw.alphaCompare==6&&a==draw.alphaRef) ||
        (draw.alphaCompare==7&&a!=draw.alphaRef);
}
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
    // Narkowicz ACES approximation: preserves highlight colour and rolls
    // radiance off smoothly instead of clipping the GGX lobe to white.
    return clamp((value*(2.51*value+0.03))/(value*(2.43*value+0.59)+0.14),0.0,1.0);
}
vec2 environmentBrdfApproximation(float perceptualRoughness,float noV) {
    const vec4 c0=vec4(-1.0,-0.0275,-0.572,0.022);
    const vec4 c1=vec4(1.0,0.0425,1.04,-0.04);
    vec4 r=perceptualRoughness*c0+c1;
    float a004=min(r.x*r.x,exp2(-9.28*noV))*r.x+r.y;
    return vec2(-1.04,1.04)*a004+r.zw;
}
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
    // Separate road chunks were exported with independently averaged vertex
    // normals.  Diffuse lighting hides the tiny mismatch, but a cubemap turns
    // it into a hard reflection seam.  For upward-facing static world
    // surfaces use the actual triangle plane as the macro normal; the normal
    // map is applied below and keeps all authored micro detail.  Vehicle and
    // character profiles retain their smooth vertex normals.
    vec3 geometricNormal=normalize(cross(dFdx(viewPositionOut),dFdy(viewPositionOut)));
    if(dot(geometricNormal,N)<0.0) geometricNormal=-geometricNormal;
    vec3 worldVertexNormal=normalize(mat3(draw.reflectionViewToWorld)*N);
    if(model==2&&profile==1&&worldVertexNormal.y>0.65)
        N=geometricNormal;
    if(model==2&&hasPbrFlag(1.0)) {
        vec2 normalXY=texture(pbrTextures,uv).rg*2.0-1.0;
        // Pure3D's texture V axis is opposite to the conventional tangent-space
        // PNG axis after its negative-pitch upload. Convert handedness here;
        // colour/roughness maps only need the row flip, vector maps need Y too.
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
        // Keep surface lighting and CSM as separate, stable decisions. Feeding
        // hardware-PCF values into N.L created small islands on smooth skin.
        float rawNdl=dot(N,L);
        float edge=max(fwidth(rawNdl)*0.65,0.035);
        float lightBand=smoothstep(-0.08-edge,0.02+edge,rawNdl);
        float csm=draw.shadowParams.x>0.5?csmShadow():0.0;
        // Only the solid interior of hardware PCF becomes the dark toon band.
        // This prevents low non-zero values in distant cascades from painting
        // an entire terrain section dark.
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

    // Cook-Torrance GGX, matching the metallic/roughness workflow used by
    // current engines. Legacy material profiles provide sensible defaults in
    // place of unavailable authored PBR maps.
    // Pure3D's Vulkan colour path already supplies the game-calibrated
    // working colour. Applying an additional sRGB decode here darkens it a
    // second time, so use it directly as the BRDF base colour.
    vec3 pbrAlbedo=hasPbrFlag(8.0)?clamp(albedo,vec3(0.0),vec3(1.0)):
                                      srgbToLinear(clamp(albedo,vec3(0.0),vec3(1.0)));
    // Missing maps are constant PBR inputs, exactly like one-pixel textures:
    // dielectric black metallic, mid roughness, flat geometric normal and AO 1.
    float metallic=metal;
    // Legacy assets without a roughness sidecar are predominantly painted,
    // concrete, asphalt and plaster rather than polished plastic.  A 0.45
    // fallback made the whole world carry an unrealistically bright, coherent
    // environment lobe.  Authored roughness maps still replace this value.
    float roughness=clamp(0.65+0.25*rough-0.20*metal,0.05,1.0);
    vec4 authoredPbr=texture(pbrTextures,uv);
    if(hasPbrFlag(2.0)) roughness=clamp(authoredPbr.b,0.089,1.0);
    if(hasPbrFlag(4.0)) metallic=authoredPbr.a;
    // Normal-map detail smaller than one screen pixel must widen the
    // microfacet lobe. Without this geometric specular AA, dark roughness
    // texels collapse into unstable, over-bright sun pixels at close range.
    vec3 normalDx=dFdx(N),normalDy=dFdy(N);
    float normalVariance=0.15*(dot(normalDx,normalDx)+dot(normalDy,normalDy));
    float kernelRoughness=min(2.0*normalVariance,0.20);
    roughness=clamp(sqrt(roughness*roughness+kernelRoughness),0.089,1.0);
    float authoredAo=1.0;
    float ndv=max(dot(N,V),0.001),ndh=max(dot(N,H),0.0),vdh=max(dot(V,H),0.0);
    float a=roughness*roughness;
    // The sun is a disk about 0.53 degrees wide, not a mathematical delta.
    // Convolve its angular variance with GGX so extremely smooth texels cannot
    // collapse into a sub-pixel, unbounded highlight.
    const float sunAngularRadius=0.00465;
    float a2=a*a+sunAngularRadius*sunAngularRadius;
    float denominator=ndh*ndh*(a2-1.0)+1.0;
    float D=a2/max(PI*denominator*denominator,0.0001);
    vec3 F0=mix(vec3(0.04),pbrAlbedo,metallic);
    vec3 F=F0+(1.0-F0)*pow(1.0-vdh,5.0);
    // Height-correlated Smith GGX visibility (Khronos glTF / Filament).
    // Unlike the old Schlick approximation this remains stable at grazing
    // angles and does not amplify the lobe through two independent masks.
    float visibilityDenominator=ndl*sqrt(ndv*ndv*(1.0-a2)+a2)+
                                ndv*sqrt(ndl*ndl*(1.0-a2)+a2);
    float visibility=0.5/max(visibilityDenominator,0.0001);
    vec3 specular=D*visibility*F;
    vec3 diffuse=(1.0-F)*(1.0-metallic)*pbrAlbedo/PI;
    vec3 worldNormal=normalize(mat3(draw.reflectionViewToWorld)*N);
    vec3 worldIncident=normalize(mat3(draw.reflectionViewToWorld)*viewPositionOut);
    vec3 reflectionDirection=normalize(reflect(worldIncident,worldNormal));
    float skyWeight=clamp(worldNormal.y*0.5+0.5,0.0,1.0);
    vec3 irradiance=mix(vec3(0.20,0.21,0.23),vec3(0.32,0.35,0.40),skyWeight);
    vec3 ambient=pbrAlbedo*irradiance*(1.0-metallic)*authoredAo;
    // Metals need an environment even when the optional reflection feature is
    // disabled: otherwise metallic removes diffuse and roughness appears to
    // remove the last visible highlight. The analytic sky/ground probe is the
    // inexpensive baseline; configured static/dynamic reflections replace it.
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
            vec2 reflectionUV=clamp(vec2(0.5+0.5*reflectionDirection.x,
                                         0.5-0.5*reflectionDirection.y),
                                    vec2(0.001),vec2(0.999));
            float maximumLod=float(max(textureQueryLevels(reflectionTexture)-1,0));
            reflectionSample=textureLod(reflectionTexture,reflectionUV,
                                        roughness*maximumLod).rgb;
        }
        vec3 sampledEnvironment=srgbToLinear(max(reflectionSample,vec3(0.0)));
        // A newly captured face can legitimately contain very dark geometry,
        // but it must not remove all energy from a metallic BRDF. Preserve the
        // analytic sky/ground probe until the sampled environment carries a
        // usable signal.
        float sampleLuminance=dot(sampledEnvironment,vec3(0.2126,0.7152,0.0722));
        environmentLinear=mix(environmentLinear,sampledEnvironment,
                              smoothstep(0.002,0.03,sampleLuminance));
    }
    vec2 environmentDfg=environmentBrdfApproximation(roughness,ndv);
    ambient+=environmentLinear*(F0*environmentDfg.x+vec3(environmentDfg.y))*authoredAo;
    // Direct PBR lighting is independent of environment reflections. Start
    // with the enhanced sun and then evaluate the same GGX BRDF for every
    // active legacy light so normal/roughness remain visible with reflections
    // disabled.
    const vec3 sunRadiance=vec3(2.15,2.04,1.86);
    float shadowAmount=draw.shadowParams.x>0.5?csmShadow():0.0;
    // CSM is directional-light visibility, not ambient occlusion.  A fully
    // covered texel loses almost all direct solar diffuse/specular while IBL
    // remains available from the rest of the hemisphere.
    float sunVisibility=1.0-0.90*shadowAmount;
    vec3 direct=(diffuse+specular)*sunRadiance*ndl*sunVisibility;
    // Model 2 returns scene-linear radiance. The caller composes every
    // remaining light and fog before the single display transform.
    return ambient+direct;
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
    outputColour=colour*texture(diffuseTexture,uv);
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
        if(kAlphaTest&&draw.alphaRef>=0.0&&!alphaPass(outputColour.a)) discard;
        return;
    }
    if(draw.outputParams.y>0.5) outputColour.rgb=enhancedLighting(outputColour.rgb);
    else outputColour.rgb+=specularLight;
    if(draw.shadowParams.x>0.5&&enhancedModel!=2&&enhancedModel!=3) {
        outputColour.rgb*=1.0-0.435*csmShadow();
    }
    vec3 rearLight=vehicleRearLightContribution();
    if(pbr) outputColour.rgb+=rearLight;
    else outputColour.rgb=clamp(outputColour.rgb+rearLight,0.0,1.0);
    if(draw.fogParams.x>0.5) {
        float range=max(draw.fogParams.z-draw.fogParams.y,0.0001);
        float amount=clamp((viewDepth-draw.fogParams.y)/range,0.0,1.0);
        vec3 fog=pbr?srgbToLinear(draw.fogColour.rgb):draw.fogColour.rgb;
        outputColour.rgb=mix(outputColour.rgb,fog,amount);
    }
    if(pbr) outputColour.rgb=linearToSrgb(filmicToneMap(outputColour.rgb));
    if(kAlphaTest&&draw.alphaRef>=0.0&&!alphaPass(outputColour.a)) discard;
}
