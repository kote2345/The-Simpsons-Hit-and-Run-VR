#version 450
layout(constant_id=0) const bool kAlphaTest=true;
layout(location=0) in vec4 colour;
layout(location=1) in vec2 uv;
layout(location=0) out vec4 outputColour;
layout(set=0,binding=0) uniform sampler2D diffuseTexture;
layout(set=1,binding=0,std140) uniform CompactDraw {
    vec4 colour;
    vec4 alpha;
} draw;
void main() {
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
