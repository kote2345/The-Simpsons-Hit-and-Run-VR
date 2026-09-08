// Shared metallic/roughness model for sunlight, punctual lights and IBL.
// GGX + correlated Smith + Schlick, with approximate multiple-scattering
// compensation following Filament's scaled specular lobe. Our DFG uses the
// classic A*F0+B convention, so white directional albedo is A+B, NOT B.
vec2 environmentBrdfApproximation(float roughness,float noV) {
    vec4 r=roughness*vec4(-1.0,-0.0275,-0.572,0.022)+vec4(1.0,0.0425,1.04,-0.04);
    float a004=min(r.x*r.x,exp2(-9.28*noV))*r.x+r.y;
    return vec2(-1.04,1.04)*a004+r.zw;
}
vec3 pbrEnergyCompensation(vec3 f0,vec2 dfg) {
    float energy=clamp(dfg.x+dfg.y,0.05,1.0);
    return vec3(1.0)+f0*(1.0/energy-1.0);
}
vec3 pbrEnvironmentReflectance(vec3 f0,vec2 dfg) {
    return clamp((f0*dfg.x+vec3(dfg.y))*pbrEnergyCompensation(f0,dfg),0.0,1.0);
}
vec3 pbrDirectBrdf(vec3 N,vec3 V,vec3 L,vec3 albedo,float metallic,
                   float roughness,float angularRadius) {
    float noL=clamp(dot(N,L),0.0,1.0);
    float noV=clamp(dot(N,V),0.0,1.0);
    if(noL<=0.0 || noV<=0.0) return vec3(0.0);
    vec3 h=L+V;
    h*=inversesqrt(max(dot(h,h),1e-12));
    float noH=clamp(dot(N,h),0.0,1.0);
    float voH=clamp(dot(V,h),0.0,1.0);
    float alpha=max(roughness*roughness,0.007921);
    float a2=alpha*alpha+angularRadius*angularRadius;
    // The previous sun denominator floor (1e-4) flattened smooth highlights.
    // Roughness and the finite sun size bound the lobe; only guard roundoff.
    float d=1.0-noH*noH+noH*noH*a2;
    float distribution=a2/max(PI*d*d,1e-12);
    float smith=0.5/max(noL*sqrt(noV*noV*(1.0-a2)+a2)+
        noV*sqrt(noL*noL*(1.0-a2)+a2),1e-7);
    vec3 f0=mix(vec3(0.04),albedo,metallic);
    vec3 fresnel=f0+(1.0-f0)*pow(1.0-voH,5.0);
    // Keep the analytic direct lobe single-scattering and energy bounded.
    // The DFG approximation describes hemisphere-integrated IBL and cannot be
    // used as an angle-independent multiplier for individual light samples.
    vec3 specular=distribution*smith*fresnel;
    vec3 diffuse=(1.0-fresnel)*(1.0-metallic)*albedo/PI;
    return (diffuse+specular)*noL;
}
