#ifndef GPUMESH_H
#define GPUMESH_H

#include <QOpenGLFunctions_3_3_Core>
#include <QVector3D>
#include <QVector>
#include <cstdint>

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

class GpuMesh
{
public:
    GpuMesh() = default;
    ~GpuMesh();

    GpuMesh(const GpuMesh &) = delete;
    GpuMesh &operator=(const GpuMesh &) = delete;
    GpuMesh(GpuMesh &&other) noexcept;
    GpuMesh &operator=(GpuMesh &&other) noexcept;

    void upload(QOpenGLFunctions_3_3_Core *gl,
                const QVector<Vertex> &vertices,
                const QVector<uint32_t> &indices);

    void draw(QOpenGLFunctions_3_3_Core *gl) const;
    void drawWireframe(QOpenGLFunctions_3_3_Core *gl) const;
    void destroy(QOpenGLFunctions_3_3_Core *gl);

    bool isValid() const { return m_vao != 0; }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    int m_indexCount = 0;
};

#endif
