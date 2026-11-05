// Rigid dual-quaternion blending avoids the LBS candy-wrapper singularity.
// Translation includes IK segment extension. Uniform scale is blended
// separately (including eye-only head hiding); nonuniform transforms use LBS.
vec4 skinQuatMul(vec4 a,vec4 b) {
    return vec4(a.w*b.xyz+b.w*a.xyz+cross(a.xyz,b.xyz),a.w*b.w-dot(a.xyz,b.xyz));
}
vec4 skinRotation(mat3 m) {
    float trace=m[0][0]+m[1][1]+m[2][2];
    vec4 q;
    if(trace>0.0) {
        float s=sqrt(trace+1.0)*2.0;
        q=vec4((m[1][2]-m[2][1])/s,(m[2][0]-m[0][2])/s,(m[0][1]-m[1][0])/s,s*0.25);
    } else if(m[0][0]>m[1][1]&&m[0][0]>m[2][2]) {
        float s=sqrt(max(0.0,1.0+m[0][0]-m[1][1]-m[2][2]))*2.0;
        q=vec4(s*0.25,(m[1][0]+m[0][1])/s,(m[2][0]+m[0][2])/s,(m[1][2]-m[2][1])/s);
    } else if(m[1][1]>m[2][2]) {
        float s=sqrt(max(0.0,1.0+m[1][1]-m[0][0]-m[2][2]))*2.0;
        q=vec4((m[1][0]+m[0][1])/s,s*0.25,(m[2][1]+m[1][2])/s,(m[2][0]-m[0][2])/s);
    } else {
        float s=sqrt(max(0.0,1.0+m[2][2]-m[0][0]-m[1][1]))*2.0;
        q=vec4((m[2][0]+m[0][2])/s,(m[2][1]+m[1][2])/s,s*0.25,(m[0][1]-m[1][0])/s);
    }
    return normalize(q);
}
vec3 skinRotate(vec4 q,vec3 p) {return p+2.0*cross(q.xyz,cross(q.xyz,p)+q.w*p);}
void skinVertex(vec3 source,vec3 sourceNormal,out vec4 result,out vec3 resultNormal) {
    vec4 weights=max(vec4(skinWeights,1.0-skinWeights.x-skinWeights.y-skinWeights.z),vec4(0));
    weights/=max(dot(weights,vec4(1)),0.000001);
    int reference=0;
    for(int i=1;i<4;++i)if(weights[i]>weights[reference])reference=i;
    mat3 referenceMatrix=mat3(draw.skinMatrices[skinIndices[reference]]);
    for(int i=0;i<3;++i)referenceMatrix[i]/=max(length(referenceMatrix[i]),0.000001);
    vec4 hemisphere=skinRotation(referenceMatrix),real=vec4(0),dual=vec4(0);
    float scale=0.0;
    vec4 linear=vec4(0);vec3 linearNormal=vec3(0);bool rigid=true;
    for(int i=0;i<4;++i) {
        if(weights[i]<=0.0)continue;
        mat4 bone=draw.skinMatrices[skinIndices[i]];
        linear+=bone*vec4(source,1)*weights[i];
        linearNormal+=mat3(bone)*sourceNormal*weights[i];
        vec3 sizes=vec3(length(bone[0].xyz),length(bone[1].xyz),length(bone[2].xyz));
        float size=(sizes.x+sizes.y+sizes.z)/3.0;
        mat3 rotation=mat3(bone)/max(size,0.000001);
        if(size<0.000001||abs(sizes.x-sizes.y)+abs(sizes.y-sizes.z)>max(size*0.001,0.000001)||
           abs(dot(rotation[0],rotation[1]))>0.001||determinant(rotation)<0.0)rigid=false;
        vec4 q=skinRotation(rotation);
        if(dot(q,hemisphere)<0.0)q=-q;
        real+=q*weights[i];
        dual+=0.5*skinQuatMul(vec4(bone[3].xyz,0),q)*weights[i];
        scale+=size*weights[i];
    }
    float magnitude=length(real);
    if(!rigid||magnitude<0.000001){result=linear;resultNormal=linearNormal;return;}
    real/=magnitude;dual/=magnitude;dual-=real*dot(real,dual);
    vec3 translation=2.0*skinQuatMul(dual,vec4(-real.xyz,real.w)).xyz;
    result=vec4(skinRotate(real,source*scale)+translation,1);
    resultNormal=skinRotate(real,sourceNormal);
}
