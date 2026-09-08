#version 450
layout(constant_id=1) const bool kHdrScene=false;
layout(constant_id=0) const bool kAlphaTest=true;
layout(location=0) in vec4 colour;
layout(location=1) in vec2 uv;
layout(location=0) out vec4 outputColour;
layout(set=0,binding=0) uniform sampler2D diffuseTexture;
layout(set=1,binding=0,std140) uniform CompactDraw {
    vec4 colour;
    vec4 alpha;
} draw;
void shadeMaterial() {
    outputColour=colour*texture(diffuseTexture,uv);
    if(kAlphaTest&&draw.alpha.x>=0.0) {
        int compare=int(draw.alpha.y+0.5);
        bool pass=compare==1 || (compare==2&&outputColour.a<draw.alpha.x) ||
            (compare==3&&outputColour.a<=draw.alpha.x) ||
            (compare==4&&outputColour.a>draw.alpha.x) ||
            (compare==5&&outputColour.a>=draw.alpha.x) ||
            (compare==6&&outputColour.a==draw.alpha.x) ||
            (compare==7&&outputColour.a!=draw.alpha.x);
        if(!pass) discard;
    }
}
void main() {
    shadeMaterial();
    if(kHdrScene) {
        vec3 c=max(outputColour.rgb,vec3(0.0));
        outputColour.rgb=mix(pow((c+0.055)/1.055,vec3(2.4)),c/12.92,
                              lessThanEqual(c,vec3(0.04045)));
    }
}
