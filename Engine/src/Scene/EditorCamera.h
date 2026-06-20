#pragma once

#include "Core/Events/Event.h"
#include "Core/Events/MouseEvent.h"
#include "Core/Time/Timestep.h"

#include <glm/glm.hpp>

namespace Conqueror
{
    enum class EditorCameraMode
    {
        Perspective3D = 0,
        Orthographic2D
    };

    class CQ_API EditorCamera
    {
    public:
        EditorCamera() = default;
        EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

        void OnUpdate(Timestep ts);
        void OnEvent(Event& e);

        void SetCameraMode(EditorCameraMode mode);
        EditorCameraMode GetCameraMode() const { return m_CameraMode; }
        bool Is2D() const { return m_CameraMode == EditorCameraMode::Orthographic2D; }

        float GetDistance() const { return m_Distance; }
        void SetDistance(float distance) { m_Distance = distance; }
        
        void SetPosition(const glm::vec3& position) { m_Position = position; UpdateView(); }
        void SetPitch(float pitch) { m_Pitch = pitch; UpdateView(); }
        void SetYaw(float yaw) { m_Yaw = yaw; UpdateView(); }

        void SetViewportSize(float width, float height) { m_ViewportWidth = width; m_ViewportHeight = height; UpdateProjection(); }

        const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
        const glm::mat4& GetProjectionMatrix() const { return m_Projection; }
        glm::mat4 GetViewProjection() const { return m_Projection * m_ViewMatrix; }

        glm::vec3 GetUpDirection() const;
        glm::vec3 GetRightDirection() const;
        glm::vec3 GetForwardDirection() const;
        const glm::vec3& GetPosition() const { return m_Position; }
        glm::quat GetOrientation() const;

        float GetPitch() const { return m_Pitch; }
        float GetYaw() const { return m_Yaw; }
        float GetOrthographicSize() const { return m_OrthographicSize; }
        void SetOrthographicSize(float size) { m_OrthographicSize = size; UpdateProjection(); }

    private:
        void UpdateProjection();
        void UpdateView();

        bool OnMouseScroll(MouseScrolledEvent& e);

        void MousePan(const glm::vec2& delta);
        void MousePan2D(const glm::vec2& delta);
        void MouseRotate(const glm::vec2& delta);
        void MouseZoom(float delta);
        void MouseZoom2D(float delta);

        glm::vec3 CalculatePosition() const;

        std::pair<float, float> PanSpeed() const;
        float RotationSpeed() const;
        float ZoomSpeed() const;

    private:
        float m_FOV = 45.0f, m_AspectRatio = 1.778f, m_NearClip = 0.1f, m_FarClip = 100000.0f;

        glm::mat4 m_ViewMatrix;
        glm::mat4 m_Projection{ 1.0f };
        glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
        glm::vec3 m_FocalPoint = { 0.0f, 0.0f, 0.0f };

        glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };

        float m_Distance = 10.0f;
        float m_Pitch = 0.5235987f, m_Yaw = 0.7853981f;

        float m_ViewportWidth = 1280, m_ViewportHeight = 720;

        EditorCameraMode m_CameraMode = EditorCameraMode::Perspective3D;
        float m_OrthographicSize = 10.0f;
    };
}
