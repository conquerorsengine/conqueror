#include "EditorCamera.h"
#include "Core/Input/Input.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Conqueror
{
    EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
        : m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
    {
        UpdateView();
    }

    void EditorCamera::SetCameraMode(EditorCameraMode mode)
    {
        if (m_CameraMode == mode)
            return;

        m_CameraMode = mode;

        if (m_CameraMode == EditorCameraMode::Orthographic2D)
        {
            m_FocalPoint.z = 0.0f;
            m_Pitch = 0.0f;
            m_Yaw = 0.0f;
            if (m_Distance < 5.0f)
                m_Distance = 10.0f;
            m_OrthographicSize = glm::max(m_Distance * 0.5f, 5.0f);
        }
        else
        {
            m_Pitch = glm::radians(30.0f);
            m_Yaw = glm::radians(45.0f);
        }

        UpdateProjection();
        UpdateView();
    }

    void EditorCamera::UpdateProjection()
    {
        if (m_ViewportHeight > 0.0f)
            m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
        else
            m_AspectRatio = 1.778f;

        if (m_CameraMode == EditorCameraMode::Orthographic2D)
        {
            const float halfHeight = m_OrthographicSize;
            const float halfWidth = halfHeight * m_AspectRatio;
            m_Projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_NearClip, m_FarClip);
            return;
        }

        m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
    }

    void EditorCamera::UpdateView()
    {
        if (m_CameraMode == EditorCameraMode::Orthographic2D)
        {
            m_FocalPoint.z = 0.0f;
            m_Position = glm::vec3(m_FocalPoint.x, m_FocalPoint.y, m_Distance);
            m_ViewMatrix = glm::lookAt(m_Position, m_FocalPoint, glm::vec3(0.0f, 1.0f, 0.0f));
            return;
        }

        m_Position = CalculatePosition();

        glm::quat orientation = GetOrientation();
        m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
        m_ViewMatrix = glm::inverse(m_ViewMatrix);
    }

    std::pair<float, float> EditorCamera::PanSpeed() const
    {
        float x = std::min(m_ViewportWidth / 1000.0f, 2.4f);
        float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

        float y = std::min(m_ViewportHeight / 1000.0f, 2.4f);
        float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

        return { xFactor, yFactor };
    }

    float EditorCamera::RotationSpeed() const
    {
        return 0.8f;
    }

    float EditorCamera::ZoomSpeed() const
    {
        float speed = m_Distance * 0.5f;
        return std::max(speed, 0.5f);
    }

    void EditorCamera::OnUpdate([[maybe_unused]] Timestep ts)
    {
        if (Input::IsKeyPressed(Key::LeftAlt))
        {
            const glm::vec2& mouse{ Input::GetMouseX(), Input::GetMouseY() };
            glm::vec2 delta = (mouse - m_InitialMousePosition) * 0.003f;
            m_InitialMousePosition = mouse;

            if (m_CameraMode == EditorCameraMode::Orthographic2D)
            {
                if (Input::IsMouseButtonPressed(Mouse::ButtonMiddle))
                    MousePan2D(delta);
                else if (Input::IsMouseButtonPressed(Mouse::ButtonRight))
                    MouseZoom2D(delta.y);
            }
            else
            {
                if (Input::IsMouseButtonPressed(Mouse::ButtonMiddle))
                    MousePan(delta);
                else if (Input::IsMouseButtonPressed(Mouse::ButtonLeft))
                    MouseRotate(delta);
                else if (Input::IsMouseButtonPressed(Mouse::ButtonRight))
                    MouseZoom(delta.y);
            }
        }

        UpdateView();
    }

    void EditorCamera::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseScrolledEvent>(CQ_BIND_EVENT_FN(EditorCamera::OnMouseScroll));
    }

    bool EditorCamera::OnMouseScroll(MouseScrolledEvent& e)
    {
        float delta = e.GetYOffset() * 0.1f;
        if (m_CameraMode == EditorCameraMode::Orthographic2D)
            MouseZoom2D(-delta);
        else
            MouseZoom(delta);
        UpdateView();
        return false;
    }

    void EditorCamera::MousePan2D(const glm::vec2& delta)
    {
        auto [xSpeed, ySpeed] = PanSpeed();
        m_FocalPoint.x += -delta.x * xSpeed * m_OrthographicSize * 2.0f;
        m_FocalPoint.y += delta.y * ySpeed * m_OrthographicSize * 2.0f;
        m_FocalPoint.z = 0.0f;
    }

    void EditorCamera::MouseZoom2D(float delta)
    {
        m_OrthographicSize -= delta * m_OrthographicSize;
        m_OrthographicSize = std::clamp(m_OrthographicSize, 1.0f, 500.0f);
        UpdateProjection();
    }

    void EditorCamera::MousePan(const glm::vec2& delta)
    {
        auto [xSpeed, ySpeed] = PanSpeed();
        m_FocalPoint += -GetRightDirection() * delta.x * xSpeed * m_Distance;
        m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
    }

    void EditorCamera::MouseRotate(const glm::vec2& delta)
    {
        float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;
        m_Yaw += yawSign * delta.x * RotationSpeed();
        m_Pitch += delta.y * RotationSpeed();
    }

    void EditorCamera::MouseZoom(float delta)
    {
        m_Distance -= delta * ZoomSpeed();
        if (m_Distance < 1.0f)
        {
            m_FocalPoint += GetForwardDirection();
            m_Distance = 1.0f;
        }
    }

    glm::vec3 EditorCamera::GetUpDirection() const
    {
        return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::vec3 EditorCamera::GetRightDirection() const
    {
        return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    glm::vec3 EditorCamera::GetForwardDirection() const
    {
        return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
    }

    glm::vec3 EditorCamera::CalculatePosition() const
    {
        return m_FocalPoint - GetForwardDirection() * m_Distance;
    }

    glm::quat EditorCamera::GetOrientation() const
    {
        return glm::quat(glm::vec3(-m_Pitch, -m_Yaw, 0.0f));
    }
}
