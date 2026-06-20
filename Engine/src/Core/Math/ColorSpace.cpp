#include "ColorSpace.h"
#include <cmath>
#include <algorithm>

namespace Conqueror::Math
{
    static float PivotRGB(float n) {
        return (n > 0.04045f) ? std::pow((n + 0.055f) / 1.055f, 2.4f) : n / 12.92f;
    }

    static float PivotXYZ(float n) {
        return (n > 0.008856f) ? std::pow(n, 1.0f / 3.0f) : (7.787f * n) + (16.0f / 116.0f);
    }
    
    static float InvPivotXYZ(float n) {
        float n3 = n * n * n;
        return (n3 > 0.008856f) ? n3 : (n - 16.0f / 116.0f) / 7.787f;
    }
    
    static float InvPivotRGB(float n) {
        return (n > 0.0031308f) ? 1.055f * std::pow(n, 1.0f / 2.4f) - 0.055f : 12.92f * n;
    }

    CQVec3 ColorSpace::RGBtoXYZ(const CQVec3& rgb)
    {
        float r = PivotRGB(rgb.x);
        float g = PivotRGB(rgb.y);
        float b = PivotRGB(rgb.z);

        return CQVec3(
            r * 0.4124564f + g * 0.3575761f + b * 0.1804375f,
            r * 0.2126729f + g * 0.7151522f + b * 0.0721750f,
            r * 0.0193339f + g * 0.1191920f + b * 0.9503041f
        );
    }

    CQVec3 ColorSpace::XYZtoRGB(const CQVec3& xyz)
    {
        float x = xyz.x;
        float y = xyz.y;
        float z = xyz.z;

        float r = x *  3.2404542f + y * -1.5371385f + z * -0.4985314f;
        float g = x * -0.9692660f + y *  1.8760108f + z *  0.0415560f;
        float b = x *  0.0556434f + y * -0.2040259f + z *  1.0572252f;

        return CQVec3(
            std::max(0.0f, std::min(1.0f, InvPivotRGB(r))),
            std::max(0.0f, std::min(1.0f, InvPivotRGB(g))),
            std::max(0.0f, std::min(1.0f, InvPivotRGB(b)))
        );
    }

    CQVec3 ColorSpace::XYZtoCIELAB(const CQVec3& xyz)
    {
        // D65 reference white
        float ref_X =  0.95047f;
        float ref_Y =  1.00000f;
        float ref_Z =  1.08883f;

        float x = PivotXYZ(xyz.x / ref_X);
        float y = PivotXYZ(xyz.y / ref_Y);
        float z = PivotXYZ(xyz.z / ref_Z);

        return CQVec3(
            std::max(0.0f, (116.0f * y) - 16.0f),
            500.0f * (x - y),
            200.0f * (y - z)
        );
    }

    CQVec3 ColorSpace::CIELABtoXYZ(const CQVec3& lab)
    {
        float y = (lab.x + 16.0f) / 116.0f;
        float x = lab.y / 500.0f + y;
        float z = y - lab.z / 200.0f;

        float ref_X =  0.95047f;
        float ref_Y =  1.00000f;
        float ref_Z =  1.08883f;

        return CQVec3(
            ref_X * InvPivotXYZ(x),
            ref_Y * InvPivotXYZ(y),
            ref_Z * InvPivotXYZ(z)
        );
    }

    CQVec3 ColorSpace::RGBtoCIELAB(const CQVec3& rgb)
    {
        return XYZtoCIELAB(RGBtoXYZ(rgb));
    }

    CQVec3 ColorSpace::CIELABtoRGB(const CQVec3& lab)
    {
        return XYZtoRGB(CIELABtoXYZ(lab));
    }

    CQVec3 ColorSpace::RGBtoOKLAB(const CQVec3& rgb)
    {
        float l = 0.4122214708f * rgb.x + 0.5363325363f * rgb.y + 0.0514459929f * rgb.z;
        float m = 0.2119034982f * rgb.x + 0.6806995451f * rgb.y + 0.1073969566f * rgb.z;
        float s = 0.0883024619f * rgb.x + 0.2817188376f * rgb.y + 0.6299787005f * rgb.z;

        float l_ = std::cbrt(l);
        float m_ = std::cbrt(m);
        float s_ = std::cbrt(s);

        return CQVec3(
            0.2104542553f*l_ + 0.7936177850f*m_ - 0.0040720468f*s_,
            1.9779984951f*l_ - 2.4285922050f*m_ + 0.4505937099f*s_,
            0.0259040371f*l_ + 0.7827717662f*m_ - 0.8086757660f*s_
        );
    }

    CQVec3 ColorSpace::OKLABtoRGB(const CQVec3& oklab)
    {
        float l_ = oklab.x + 0.3963377774f * oklab.y + 0.2158037573f * oklab.z;
        float m_ = oklab.x - 0.1055613458f * oklab.y - 0.0638541728f * oklab.z;
        float s_ = oklab.x - 0.0894841775f * oklab.y - 1.2914855480f * oklab.z;

        float l = l_*l_*l_;
        float m = m_*m_*m_;
        float s = s_*s_*s_;

        float r =  4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
        float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
        float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
        
        return CQVec3(std::max(0.0f, std::min(1.0f, r)), std::max(0.0f, std::min(1.0f, g)), std::max(0.0f, std::min(1.0f, b)));
    }

    CQVec3 ColorSpace::OKLABtoOKLCH(const CQVec3& oklab)
    {
        float c = std::sqrt(oklab.y * oklab.y + oklab.z * oklab.z);
        float h = std::atan2(oklab.z, oklab.y);
        if (h < 0) h += 2.0f * 3.14159265f;
        return CQVec3(oklab.x, c, h);
    }

    CQVec3 ColorSpace::OKLCHtoOKLAB(const CQVec3& oklch)
    {
        return CQVec3(oklch.x, oklch.y * std::cos(oklch.z), oklch.y * std::sin(oklch.z));
    }

    float ColorSpace::DeltaE76(const CQVec3& lab1, const CQVec3& lab2)
    {
        float dl = lab1.x - lab2.x;
        float da = lab1.y - lab2.y;
        float db = lab1.z - lab2.z;
        return std::sqrt(dl*dl + da*da + db*db);
    }

    float ColorSpace::DeltaE94(const CQVec3& lab1, const CQVec3& lab2)
    {
        float dl = lab1.x - lab2.x;
        float da = lab1.y - lab2.y;
        float db = lab1.z - lab2.z;
        float c1 = std::sqrt(lab1.y*lab1.y + lab1.z*lab1.z);
        float c2 = std::sqrt(lab2.y*lab2.y + lab2.z*lab2.z);
        float dc = c1 - c2;
        float dh_sq = da*da + db*db - dc*dc;
        float dh = dh_sq > 0 ? std::sqrt(dh_sq) : 0;
        
        float sl = 1.0f;
        float sc = 1.0f + 0.045f * c1;
        float sh = 1.0f + 0.015f * c1;
        
        float dl_sl = dl/sl;
        float dc_sc = dc/sc;
        float dh_sh = dh/sh;
        
        return std::sqrt(dl_sl*dl_sl + dc_sc*dc_sc + dh_sh*dh_sh);
    }

    float ColorSpace::DeltaEOklab(const CQVec3& oklab1, const CQVec3& oklab2)
    {
        float dl = oklab1.x - oklab2.x;
        float da = oklab1.y - oklab2.y;
        float db = oklab1.z - oklab2.z;
        return std::sqrt(dl*dl + da*da + db*db);
    }

    CQVec3 ColorSpace::KelvinToRGB(float temperature)
    {
        float temp = std::max(1000.0f, std::min(40000.0f, temperature)) / 100.0f;
        float r, g, b;

        if (temp <= 66.0f) {
            r = 255.0f;
            g = temp;
            g = 99.4708025861f * std::log(g) - 161.1195681661f;
            if (temp <= 19.0f) {
                b = 0.0f;
            } else {
                b = temp - 10.0f;
                b = 138.5177312231f * std::log(b) - 305.0447927307f;
            }
        } else {
            r = temp - 60.0f;
            r = 329.698727446f * std::pow(r, -0.1332047592f);
            g = temp - 60.0f;
            g = 288.1221695283f * std::pow(g, -0.0755148492f);
            b = 255.0f;
        }

        return CQVec3(
            std::max(0.0f, std::min(255.0f, r)) / 255.0f,
            std::max(0.0f, std::min(255.0f, g)) / 255.0f,
            std::max(0.0f, std::min(255.0f, b)) / 255.0f
        );
    }
}