#include "../code/vr/vulkan/projection_math.h"
#include <cstdio>
#include <cstdlib>
#include <array>

using V=std::array<double,4>;
static V transform(const float* m,V v) {
    V result={};
    for(int r=0;r<4;++r) for(int c=0;c<4;++c) result[r]+=m[c*4+r]*v[c];
    return result;
}
static V point(const float* m,V v) {
    V result=transform(m,v);
    for(int i=0;i<3;++i) result[i]/=result[3];
    result[3]=1;
    return result;
}
static void require(bool condition,const char* message) {
    if(!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main() {
    double worst=0;
    for(float eye : {-0.032f,0.032f}) for(float handedness : {-1.0f,1.0f}) {
        // Asymmetric perspective combined with centre-view -> eye translation,
        // matching the column-major P * eyeAdjustment used by the renderer.
        const float nearZ=0.1f,farZ=500.0f;
        float p[16]={1.1f,0,0,0, 0,1.3f,0,0,
            0.14f,-0.08f,handedness*(farZ+nearZ)/(farZ-nearZ),handedness,
            -1.1f*eye,0,-2*farZ*nearZ/(farZ-nearZ),0};
        float inverse[16];
        require(SharOpenXR::InvertProjection(p,inverse),"perspective invertible");
        for(int c=0;c<4;++c) {
            V basis={}; basis[c]=1;
            V product=transform(inverse,transform(p,basis));
            for(int r=0;r<4;++r)
                require(std::fabs(product[r]-(r==c?1.0:0.0))<1e-5,"inverse * projection identity");
        }
        for(double distance : {0.1,0.5,2.0,24.0,56.0,120.0})
            for(double slope : {-0.6,0.0,0.6}) {
                V world={eye+slope*distance,0.2*distance,handedness*distance,1};
                V clip=point(p,world);
                // Simulate Vulkan depth conversion and reconstruction.
                const double depth=clip[2]*0.5+0.5;
                V reconstructed=point(inverse,{clip[0],clip[1],depth*2-1,1});
                for(int r=0;r<3;++r) {
                    const double error=std::fabs(reconstructed[r]-world[r]);
                    worst=std::fmax(worst,error);
                    require(error<0.01,"native depth reconstructs centre-view position");
                }
            }
        V leftNear=point(inverse,{-0.7,0,-1,1}),leftFar=point(inverse,{-0.7,0,1,1});
        V rightNear=point(inverse,{0.7,0,-1,1}),rightFar=point(inverse,{0.7,0,1,1});
        double leftSlope=(leftFar[0]-leftNear[0])/(leftFar[2]-leftNear[2]);
        double rightSlope=(rightFar[0]-rightNear[0])/(rightFar[2]-rightNear[2]);
        require(std::fabs(leftSlope-rightSlope)>0.5,"froxel rays diverge across the screen");
    }
    float zero[16]={},inverse[16];
    require(!SharOpenXR::InvertProjection(zero,inverse),"singular projection rejected");
    std::printf("PASS: stereo projective round trips, diverging rays, singular input; max error %.8f units\n",worst);
}
