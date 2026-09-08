#ifndef SHAR_VULKAN_PROJECTION_MATH_H
#define SHAR_VULKAN_PROJECTION_MATH_H

#include <cmath>
#include <utility>

namespace SharOpenXR {
// Full projective inverse for column-major shader matrices. radmath::Invert
// only inverts the affine 3x3 + translation and resets the projective column.
inline bool InvertProjection(const float input[16], float output[16]) {
    double a[4][8]={};
    for(unsigned row=0;row<4;++row) {
        for(unsigned col=0;col<4;++col) {
            if(!std::isfinite(input[col*4+row])) return false;
            a[row][col]=input[col*4+row];
        }
        a[row][row+4]=1.0;
    }
    for(unsigned col=0;col<4;++col) {
        unsigned pivot=col;
        for(unsigned row=col+1;row<4;++row)
            if(std::fabs(a[row][col])>std::fabs(a[pivot][col])) pivot=row;
        if(std::fabs(a[pivot][col])<1e-12) return false;
        for(unsigned k=0;k<8;++k) std::swap(a[col][k],a[pivot][k]);
        const double scale=a[col][col];
        for(unsigned k=0;k<8;++k) a[col][k]/=scale;
        for(unsigned row=0;row<4;++row) if(row!=col) {
            const double factor=a[row][col];
            for(unsigned k=0;k<8;++k) a[row][k]-=factor*a[col][k];
        }
    }
    for(unsigned row=0;row<4;++row)
        for(unsigned col=0;col<4;++col) {
            const float value=static_cast<float>(a[row][col+4]);
            if(!std::isfinite(value)) return false;
            output[col*4+row]=value;
        }
    return true;
}
}
#endif
