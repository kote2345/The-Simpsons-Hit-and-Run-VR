// Shared by both PCVR PBR geometry paths. All vectors must use the same space.
// Returns the BRDF times N.L; radiance and visibility are supplied by the caller.
vec3 punctualBrdf(vec3 N,vec3 V,vec3 L,vec3 albedo,float metallic,float roughness) {
    return pbrDirectBrdf(N,V,L,albedo,metallic,roughness,0.0);
}

vec3 pbrRearLights(vec3 N,vec3 V,vec3 albedo,float metallic,float roughness) {
    int mode=int(draw.vehicleRearLightParams.w+0.5);
    int count=clamp(int(draw.vehicleRearLightControl.x+0.5),0,4);
    if(mode==0 || count==0) return vec3(0.0);
    vec3 sum=vec3(0.0);
    float radius=mode==1?5.0:7.0;
    vec3 radiance=srgbToLinear(clamp(draw.vehicleRearLightParams.rgb,0.0,1.0))*PI;
    for(int i=0;i<count;++i) {
        vec3 fromLamp=viewPositionOut-draw.vehicleRearLightPositions[i].xyz;
        float d2=dot(fromLamp,fromLamp);
        if(d2>=radius*radius) continue;
        float distanceToLight=sqrt(max(d2,0.000001));
        vec3 ray=fromLamp/distanceToLight;
        float cone=smoothstep(0.48,0.78,dot(ray,draw.vehicleRearLightDirections[i].xyz));
        if(cone<=0.0) continue;
        float falloff=1.0-distanceToLight/radius;
        float attenuation=falloff*falloff*smoothstep(0.30,0.85,distanceToLight)*cone;
        sum+=punctualBrdf(N,V,-ray,albedo,metallic,roughness)*radiance*attenuation;
    }
    return sum;
}
