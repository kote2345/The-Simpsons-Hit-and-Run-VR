// Cumulative values live at segment ends, with an implicit (0,0,0,1)
// boundary at the near plane. Resolve at native pixel depth, never filter
// a 2D image after depth termination (that mixes different ray lengths).
const int sliceCount=96;
const float maximumDistance=120.0;
float sliceDistance(float t) { return (exp2(t*log2(1.0+maximumDistance))-1.0); }
vec3 volumeUnproject(vec2 p,float z) {
    vec4 h=camera.inverseProjection*vec4(p.x*2-1,1-p.y*2,z,1);
    return h.xyz/(abs(h.w)<1e-6?(h.w<0?-1e-6:1e-6):h.w);
}
vec4 sampleVolumeAtDepth(vec2 p,int view,float z) {
    float distance=z>=1.0?maximumDistance:
        min(length(volumeUnproject(p,z*2-1)-volumeUnproject(p,-1)),maximumDistance);
    float boundary=log2(1.0+distance)/log2(1.0+maximumDistance)*float(sliceCount);
    int end=min(int(floor(boundary)),sliceCount-1);
    float d0=sliceDistance(float(end)/float(sliceCount));
    float d1=sliceDistance(float(end+1)/float(sliceCount));
    float f=clamp((distance-d0)/(d1-d0),0.0,1.0);
    vec4 a=end==0?vec4(0,0,0,1):textureLod(froxelVolume,vec3(p,view*sliceCount+end-1),0);
    vec4 b=textureLod(froxelVolume,vec3(p,view*sliceCount+end),0);
    // Beer-Lambert interpolation within a segment, including its source term.
    float ratio=clamp(b.a/max(a.a,1e-6),1e-6,1.0);
    float partial=pow(ratio,f);
    float weight=ratio<0.9999?(1.0-partial)/(1.0-ratio):f;
    return vec4(mix(a.rgb,b.rgb,weight),a.a*partial);
}
vec4 sampleVolume(vec2 p,int view) {
    return sampleVolumeAtDepth(p,view,textureLod(depth,vec3(p,view),0).r);
}
