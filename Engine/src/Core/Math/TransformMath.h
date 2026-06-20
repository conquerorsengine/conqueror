#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include "QuaternionMath.h"
#include "MatrixMath.h"

namespace Conqueror::Math
{
    struct CQ_API CQTransform
    {
        CQVec3 Position{0.0f, 0.0f, 0.0f};
        CQQuat Rotation{0.0f, 0.0f, 0.0f, 1.0f};
        CQVec3 Scale{1.0f, 1.0f, 1.0f};

        CQMat4 GetLocalMatrix() const;
        CQVec3 GetForward() const;
        CQVec3 GetRight() const;
        CQVec3 GetUp() const;

        void Translate(const CQVec3& translation);
        void Rotate(const CQQuat& rotation);
        void RotateEuler(const CQVec3& eulerAngles);
        void LookAt(const CQVec3& target, const CQVec3& up = {0.0f, 1.0f, 0.0f});
    };

    class CQ_API TransformHierarchy
    {
    public:
        static CQMat4 CalculateGlobalMatrix(const CQMat4& parentGlobal, const CQMat4& local);
        static CQTransform ExtractTransform(const CQMat4& globalMatrix);
        static CQVec3 TransformPoint(const CQMat4& matrix, const CQVec3& point);
        static CQVec3 TransformDirection(const CQMat4& matrix, const CQVec3& direction);
    };
}