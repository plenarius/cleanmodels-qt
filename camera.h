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

private:
    QVector3D m_target;
    float m_distance = 10.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.6f;
    float m_fov = 45.0f;
    float m_aspect = 1.0f;
    float m_near = 0.01f;
    float m_far = 5000.0f;
};

#endif
