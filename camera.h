#ifndef CAMERA_H
#define CAMERA_H

#include <QMatrix4x4>
#include <QVector3D>

class Camera
{
public:
    Camera();

    void rotate(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float delta);
    void focusOnBounds(const QVector3D &bmin, const QVector3D &bmax);
    void setAspectRatio(float aspect);

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix() const;
    QVector3D position() const;

    float rotationSensitivity() const { return m_rotSens; }
    void setRotationSensitivity(float v) { m_rotSens = v; }

    float panScale() const { return m_panScale; }
    void setPanScale(float v) { m_panScale = v; }

    float zoomFactor() const { return m_zoomFactor; }
    void setZoomFactor(float v) { m_zoomFactor = v; }

    static constexpr float DefaultRotationSensitivity = 0.01f;
    static constexpr float DefaultPanScale = 0.002f;
    static constexpr float DefaultZoomFactor = 0.1f;

private:
    QVector3D m_target;
    float m_distance = 10.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.6f;
    float m_fov = 45.0f;
    float m_aspect = 1.0f;
    float m_near = 0.01f;
    float m_far = 5000.0f;

    float m_rotSens = DefaultRotationSensitivity;
    float m_panScale = DefaultPanScale;
    float m_zoomFactor = DefaultZoomFactor;
};

#endif
