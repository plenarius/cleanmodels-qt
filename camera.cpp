#include "camera.h"
#include <QtMath>
#include <algorithm>

Camera::Camera() = default;

QVector3D Camera::position() const
{
    float x = m_distance * cosf(m_pitch) * cosf(m_yaw);
    float y = m_distance * cosf(m_pitch) * sinf(m_yaw);
    float z = m_distance * sinf(m_pitch);
    return m_target + QVector3D(x, y, z);
}

QMatrix4x4 Camera::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(position(), m_target, QVector3D(0, 0, 1));
    return view;
}

QMatrix4x4 Camera::projectionMatrix() const
{
    QMatrix4x4 proj;
    proj.perspective(m_fov, m_aspect, m_near, m_far);
    return proj;
}

void Camera::rotate(float dx, float dy)
{
    m_yaw -= dx * m_rotSens;
    m_pitch += dy * m_rotSens;
    m_pitch = std::clamp(m_pitch, -1.5f, 1.5f);
}

void Camera::pan(float dx, float dy)
{
    QVector3D forward = (m_target - position()).normalized();
    QVector3D up(0, 0, 1);
    QVector3D right = QVector3D::crossProduct(forward, up).normalized();
    QVector3D camUp = QVector3D::crossProduct(right, forward).normalized();

    float scale = m_distance * m_panScale;
    m_target += right * (-dx * scale) + camUp * (dy * scale);
}

void Camera::zoom(float delta)
{
    m_distance *= (1.0f - delta * m_zoomFactor);
    m_distance = std::clamp(m_distance, 0.1f, 10000.0f);
}

void Camera::focusOnBounds(const QVector3D &bmin, const QVector3D &bmax)
{
    m_target = (bmin + bmax) * 0.5f;
    float extent = (bmax - bmin).length();
    if (extent < 0.001f)
        extent = 10.0f;
    m_distance = extent * 1.5f;
    m_near = m_distance * 0.001f;
    m_far = m_distance * 100.0f;
}

void Camera::setAspectRatio(float aspect)
{
    m_aspect = aspect;
}
