// Shared PBR cascade sampling. Comparison samplers return shadow amount
// (GREATER), so zero means visible sunlight, not an occluded receiver.
vec3 shadowPosition(vec4 coordinate) {
    vec3 p=coordinate.xyz/coordinate.w; p.y=-p.y; p=p*0.5+0.5;
    return p;
}
bool validShadowPosition(vec3 p) {
    return p.x>0.001&&p.x<0.999&&p.y>0.001&&p.y<0.999&&p.z>0.0&&p.z<1.0;
}
// Fixed texture-space kernel: no frame noise or eye-dependent screen rotation.
// Keep each sampler bound statically for mobile driver compatibility.
float sampleShadow0(vec4 coordinate) {
    vec3 p=shadowPosition(coordinate);
    if(!validShadowPosition(p)) return 0.0;
    float reference=p.z-draw.shadowParams.w;
    float radius=draw.pbrMapControl.w;
    if(radius<=0.0)
        return texture(shadowTexture0,vec3(p.xy,reference));
    vec2 texel=1.0/vec2(textureSize(shadowTexture0,0));
    float sum=0.0;
    for(int y=-1;y<=1;++y) {
        for(int x=-1;x<=1;++x) {
            float weight=float((x==0?2:1)*(y==0?2:1));
            vec2 offset=vec2(x,y)*texel*radius;
            sum+=weight*texture(shadowTexture0,vec3(p.xy+offset,reference));
        }
    }
    return sum/16.0;
}
float sampleShadow1(vec4 coordinate) {
    vec3 p=shadowPosition(coordinate);
    if(!validShadowPosition(p)) return 0.0;
    float reference=p.z-draw.shadowParams.w;
    float radius=draw.pbrMapControl.w;
    if(radius<=0.0)
        return texture(shadowTexture1,vec3(p.xy,reference));
    vec2 texel=1.0/vec2(textureSize(shadowTexture1,0));
    float sum=0.0;
    for(int y=-1;y<=1;++y) {
        for(int x=-1;x<=1;++x) {
            float weight=float((x==0?2:1)*(y==0?2:1));
            vec2 offset=vec2(x,y)*texel*radius;
            sum+=weight*texture(shadowTexture1,vec3(p.xy+offset,reference));
        }
    }
    return sum/16.0;
}
float sampleShadow2(vec4 coordinate) {
    vec3 p=shadowPosition(coordinate);
    if(!validShadowPosition(p)) return 0.0;
    float reference=p.z-draw.shadowParams.w;
    float radius=draw.pbrMapControl.w;
    if(radius<=0.0)
        return texture(shadowTexture2,vec3(p.xy,reference));
    vec2 texel=1.0/vec2(textureSize(shadowTexture2,0));
    float sum=0.0;
    for(int y=-1;y<=1;++y) {
        for(int x=-1;x<=1;++x) {
            float weight=float((x==0?2:1)*(y==0?2:1));
            vec2 offset=vec2(x,y)*texel*radius;
            sum+=weight*texture(shadowTexture2,vec3(p.xy+offset,reference));
        }
    }
    return sum/16.0;
}
float csmShadow() {
    if(draw.pbrMapControl.w>0.0) {
        // Fall back to the next cascade when a receiver leaves coverage.
        bool valid0=validShadowPosition(shadowPosition(shadowCoord0));
        bool valid1=validShadowPosition(shadowPosition(shadowCoord1));
        bool valid2=validShadowPosition(shadowPosition(shadowCoord2));
        if(viewDepth<24.0 && valid0) {
            if(viewDepth<20.0 || !valid1) return sampleShadow0(shadowCoord0);
            return mix(sampleShadow0(shadowCoord0),sampleShadow1(shadowCoord1),
                       smoothstep(20.0,24.0,viewDepth));
        }
        if(viewDepth<56.0 && valid1) {
            if(viewDepth<50.0 || !valid2) return sampleShadow1(shadowCoord1);
            return mix(sampleShadow1(shadowCoord1),sampleShadow2(shadowCoord2),
                       smoothstep(50.0,56.0,viewDepth));
        }
        if(valid2) return sampleShadow2(shadowCoord2);
        if(valid1) return sampleShadow1(shadowCoord1);
        if(valid0) return sampleShadow0(shadowCoord0);
        return 0.0;
    }
    // Original standalone path, including its established cascade blending.
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
