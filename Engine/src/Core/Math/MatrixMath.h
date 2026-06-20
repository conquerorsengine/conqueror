#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <array>

namespace Conqueror::Math
{
    // Motor seviyesinde saf matris tipi (glm bağımsız)
    using CQMat4 = std::array<float, 16>;
    using CQMat3 = std::array<float, 9>;

    CQ_API CQMat3 Mat3Identity();
    CQ_API float Mat3Determinant(const CQMat3& m);
    CQ_API CQMat3 Mat3Transpose(const CQMat3& m);
    CQ_API CQMat3 Mat3Multiply(const CQMat3& a, const CQMat3& b);

    CQ_API CQMat4 Mat4Identity();
    CQ_API CQMat4 Mat4Translate(const CQMat4& m, const CQVec3& v);
    CQ_API CQMat4 Mat4Rotate(const CQMat4& m, float angle, const CQVec3& axis);
    CQ_API CQMat4 Mat4Scale(const CQMat4& m, const CQVec3& s);
    CQ_API CQMat4 Mat4Multiply(const CQMat4& a, const CQMat4& b);
    CQ_API CQMat4 Mat4Inverse(const CQMat4& m);
    CQ_API CQMat4 Mat4Transpose(const CQMat4& m);
    CQ_API CQMat4 Mat4LookAt(const CQVec3& eye, const CQVec3& center, const CQVec3& up);
    CQ_API CQMat4 Mat4Perspective(float fov, float aspect, float zNear, float zFar);
    CQ_API CQMat4 Mat4Ortho(float left, float right, float bottom, float top, float zNear, float zFar);
    CQ_API CQMat3 Mat3Inverse(const CQMat3& m);
    CQ_API bool Mat4Decompose(const CQMat4& m, CQVec3& translation, CQVec3& rotation, CQVec3& scale);
    CQ_API CQMat4 Mat4FromTRS(const CQVec3& translation, const CQVec3& rotation, const CQVec3& scale);
}
