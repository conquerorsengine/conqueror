#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"

namespace Conqueror::Math
{
    class CQ_API ColorSpace
    {
    public:
        // Core Conversions
        static CQVec3 RGBtoXYZ(const CQVec3& rgb);
        static CQVec3 XYZtoRGB(const CQVec3& xyz);
        
        static CQVec3 XYZtoCIELAB(const CQVec3& xyz);
        static CQVec3 CIELABtoXYZ(const CQVec3& lab);
        
        static CQVec3 RGBtoCIELAB(const CQVec3& rgb);
        static CQVec3 CIELABtoRGB(const CQVec3& lab);
        
        // Modern OKLAB / OKLCH
        static CQVec3 RGBtoOKLAB(const CQVec3& rgb);
        static CQVec3 OKLABtoRGB(const CQVec3& oklab);
        
        static CQVec3 OKLABtoOKLCH(const CQVec3& oklab);
        static CQVec3 OKLCHtoOKLAB(const CQVec3& oklch);
        
        // Color Differences (Perceptual)
        static float DeltaE76(const CQVec3& lab1, const CQVec3& lab2);
        static float DeltaE94(const CQVec3& lab1, const CQVec3& lab2);
        static float DeltaEOklab(const CQVec3& oklab1, const CQVec3& oklab2);
        
        // Light Simulation
        static CQVec3 KelvinToRGB(float temperature);
    };
}