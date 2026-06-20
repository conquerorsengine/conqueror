#include "MatrixMath.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Conqueror::Math
{
    static glm::vec3 ToGLM(const CQVec3& v) { return glm::vec3(v.x, v.y, v.z); }

    static glm::mat4 ToGLM(const CQMat4& m)
    {
        return glm::make_mat4(m.data());
    }

    static CQMat4 FromGLM(const glm::mat4& m)
    {
        CQMat4 result;
        const float* ptr = glm::value_ptr(m);
        for (int i = 0; i < 16; ++i) result[i] = ptr[i];
        return result;
    }

    static glm::mat3 ToGLM(const CQMat3& m)
    {
        return glm::make_mat3(m.data());
    }

    static CQMat3 FromGLM(const glm::mat3& m)
    {
        CQMat3 result;
        const float* ptr = glm::value_ptr(m);
        for (int i = 0; i < 9; ++i) result[i] = ptr[i];
        return result;
    }

    CQMat3 Mat3Identity()
    {
        return FromGLM(glm::mat3(1.0f));
    }

    float Mat3Determinant(const CQMat3& m)
    {
        return glm::determinant(ToGLM(m));
    }

    CQMat3 Mat3Transpose(const CQMat3& m)
    {
        return FromGLM(glm::transpose(ToGLM(m)));
    }

    CQMat3 Mat3Multiply(const CQMat3& a, const CQMat3& b)
    {
        return FromGLM(ToGLM(a) * ToGLM(b));
    }

    CQMat4 Mat4Identity()
    {
        return FromGLM(glm::mat4(1.0f));
    }

    CQMat4 Mat4Translate(const CQMat4& m, const CQVec3& v)
    {
        return FromGLM(glm::translate(ToGLM(m), ToGLM(v)));
    }

    CQMat4 Mat4Rotate(const CQMat4& m, float angle, const CQVec3& axis)
    {
        return FromGLM(glm::rotate(ToGLM(m), angle, ToGLM(axis)));
    }

    CQMat4 Mat4Scale(const CQMat4& m, const CQVec3& s)
    {
        return FromGLM(glm::scale(ToGLM(m), ToGLM(s)));
    }

    CQMat4 Mat4Multiply(const CQMat4& a, const CQMat4& b)
    {
        return FromGLM(ToGLM(a) * ToGLM(b));
    }

    CQMat4 Mat4Inverse(const CQMat4& m)
    {
        return FromGLM(glm::inverse(ToGLM(m)));
    }

    CQMat4 Mat4Transpose(const CQMat4& m)
    {
        return FromGLM(glm::transpose(ToGLM(m)));
    }

    CQMat4 Mat4LookAt(const CQVec3& eye, const CQVec3& center, const CQVec3& up)
    {
        return FromGLM(glm::lookAt(ToGLM(eye), ToGLM(center), ToGLM(up)));
    }

    CQMat4 Mat4Perspective(float fov, float aspect, float zNear, float zFar)
    {
        return FromGLM(glm::perspective(fov, aspect, zNear, zFar));
    }

    CQMat4 Mat4Ortho(float left, float right, float bottom, float top, float zNear, float zFar)
    {
        return FromGLM(glm::ortho(left, right, bottom, top, zNear, zFar));
    }

    CQMat3 Mat3Inverse(const CQMat3& m)
    {
        return FromGLM(glm::inverse(ToGLM(m)));
    }

    bool Mat4Decompose(const CQMat4& m, CQVec3& translation, CQVec3& rotation, CQVec3& scale)
    {
        glm::mat4 mat = ToGLM(m);
        
        glm::vec3 s(glm::length(glm::vec3(mat[0])), glm::length(glm::vec3(mat[1])), glm::length(glm::vec3(mat[2])));
        if (s.x == 0.0f || s.y == 0.0f || s.z == 0.0f) return false;
        
        scale = {s.x, s.y, s.z};
        translation = {mat[3][0], mat[3][1], mat[3][2]};

        glm::mat3 rotMat(
            glm::vec3(mat[0]) / s.x,
            glm::vec3(mat[1]) / s.y,
            glm::vec3(mat[2]) / s.z
        );
        
        float sy = std::sqrt(rotMat[0][0] * rotMat[0][0] + rotMat[1][0] * rotMat[1][0]);
        bool singular = sy < 1e-6;

        float x, y, z;
        if (!singular) {
            x = std::atan2(rotMat[2][1], rotMat[2][2]);
            y = std::atan2(-rotMat[2][0], sy);
            z = std::atan2(rotMat[1][0], rotMat[0][0]);
        } else {
            x = std::atan2(-rotMat[1][2], rotMat[1][1]);
            y = std::atan2(-rotMat[2][0], sy);
            z = 0;
        }
        
        rotation = {x, y, z};
        return true;
    }

    CQMat4 Mat4FromTRS(const CQVec3& translation, const CQVec3& rotation, const CQVec3& scale)
    {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), ToGLM(translation));
        
        glm::mat4 rx = glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::mat4 ry = glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 rz = glm::rotate(glm::mat4(1.0f), rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 r = rz * ry * rx;
        
        glm::mat4 s = glm::scale(glm::mat4(1.0f), ToGLM(scale));
        
        return FromGLM(t * r * s);
    }
}
