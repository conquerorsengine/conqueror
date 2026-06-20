#include "TransformMath.h"

namespace Conqueror::Math
{
    CQMat4 CQTransform::GetLocalMatrix() const
    {
        CQMat4 t = Mat4Translate(Mat4Identity(), Position);
        CQMat4 r = QuatToMat4(Rotation);
        CQMat4 s = Mat4Scale(Mat4Identity(), Scale);
        return Mat4Multiply(Mat4Multiply(t, r), s);
    }

    CQVec3 CQTransform::GetForward() const
    {
        return QuatMultiplyVec3(Rotation, CQVec3(0.0f, 0.0f, -1.0f));
    }

    CQVec3 CQTransform::GetRight() const
    {
        return QuatMultiplyVec3(Rotation, CQVec3(1.0f, 0.0f, 0.0f));
    }

    CQVec3 CQTransform::GetUp() const
    {
        return QuatMultiplyVec3(Rotation, CQVec3(0.0f, 1.0f, 0.0f));
    }

    void CQTransform::Translate(const CQVec3& translation)
    {
        Position = Vec3Add(Position, translation);
    }

    void CQTransform::Rotate(const CQQuat& rotation)
    {
        Rotation = QuatNormalize(QuatMultiply(Rotation, rotation));
    }

    void CQTransform::RotateEuler(const CQVec3& eulerAngles)
    {
        Rotate(QuatFromEuler(eulerAngles));
    }

    void CQTransform::LookAt(const CQVec3& target, const CQVec3& up)
    {
        CQVec3 forward = Vec3Normalize(Vec3Sub(target, Position));
        Rotation = QuatLookRotation(forward, up);
    }

    CQMat4 TransformHierarchy::CalculateGlobalMatrix(const CQMat4& parentGlobal, const CQMat4& local)
    {
        return Mat4Multiply(parentGlobal, local);
    }

    CQTransform TransformHierarchy::ExtractTransform(const CQMat4& globalMatrix)
    {
        CQTransform result;
        CQVec3 euler;
        if(Mat4Decompose(globalMatrix, result.Position, euler, result.Scale))
        {
            result.Rotation = QuatFromEuler(euler);
        }
        return result;
    }

    CQVec3 TransformHierarchy::TransformPoint(const CQMat4& matrix, const CQVec3& point)
    {
        float x = matrix[0]*point.x + matrix[4]*point.y + matrix[8]*point.z + matrix[12];
        float y = matrix[1]*point.x + matrix[5]*point.y + matrix[9]*point.z + matrix[13];
        float z = matrix[2]*point.x + matrix[6]*point.y + matrix[10]*point.z + matrix[14];
        float w = matrix[3]*point.x + matrix[7]*point.y + matrix[11]*point.z + matrix[15];
        if (w != 1.0f && w != 0.0f) { x /= w; y /= w; z /= w; }
        return {x, y, z};
    }

    CQVec3 TransformHierarchy::TransformDirection(const CQMat4& matrix, const CQVec3& direction)
    {
        float x = matrix[0]*direction.x + matrix[4]*direction.y + matrix[8]*direction.z;
        float y = matrix[1]*direction.x + matrix[5]*direction.y + matrix[9]*direction.z;
        float z = matrix[2]*direction.x + matrix[6]*direction.y + matrix[10]*direction.z;
        return {x, y, z};
    }
}