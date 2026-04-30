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

    // In-place vertex data update for animated meshes. Vertex count must
    // match the count from the most recent upload(); only the per-vertex
    // data (positions/normals/uvs) is rewritten. No-op if `gl` is null,
    // the mesh isn't valid, or the vertex count differs.
    void updateVertices(QOpenGLFunctions_3_3_Core *gl,
                        const QVector<Vertex> &vertices);

    int vertexCount() const { return m_vertexCount; }

    void draw(QOpenGLFunctions_3_3_Core *gl) const;
    void drawWireframe(QOpenGLFunctions_3_3_Core *gl) const;
    void destroy(QOpenGLFunctions_3_3_Core *gl);

    bool isValid() const { return m_vao != 0; }
    GLuint vao() const { return m_vao; }
    int indexCount() const { return m_indexCount; }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    int m_indexCount = 0;
    int m_vertexCount = 0;
    // Per-mesh latch: a count-mismatch in updateVertices() once-per-mesh
    // warns and then stays silent so a wedged animated mesh doesn't spam
    // the log at 60 Hz. Reset on upload() so a re-upload of the same
    // GpuMesh handle gets a fresh diagnostic chance.
    bool m_warnedSizeMismatch = false;
};

#endif
