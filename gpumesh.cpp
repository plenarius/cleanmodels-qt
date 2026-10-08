#include "gpumesh.h"
#include <QDebug>
#include <utility>

GpuMesh::~GpuMesh() = default;

// Member-wise std::swap on every field. This shape lets adding a new GPU
// handle (or counter) be a one-line edit to the field list and has the
// move ops pick it up automatically — previously each new field required
// manual edits to the move-ctor, move-assign, and destroy() in lockstep,
// and m_vertexCount was missed in destroy() the first time it was added.
GpuMesh::GpuMesh(GpuMesh &&other) noexcept
{
    std::swap(m_vao, other.m_vao);
    std::swap(m_vbo, other.m_vbo);
    std::swap(m_ebo, other.m_ebo);
    std::swap(m_indexCount, other.m_indexCount);
    std::swap(m_vertexCount, other.m_vertexCount);
    std::swap(m_warnedSizeMismatch, other.m_warnedSizeMismatch);
}

GpuMesh &GpuMesh::operator=(GpuMesh &&other) noexcept
{
    if (this != &other) {
        std::swap(m_vao, other.m_vao);
        std::swap(m_vbo, other.m_vbo);
        std::swap(m_ebo, other.m_ebo);
        std::swap(m_indexCount, other.m_indexCount);
        std::swap(m_vertexCount, other.m_vertexCount);
        std::swap(m_warnedSizeMismatch, other.m_warnedSizeMismatch);
    }
    return *this;
}

void GpuMesh::upload(QOpenGLFunctions_3_3_Core *gl,
                     const QVector<Vertex> &vertices,
                     const QVector<uint32_t> &indices)
{
    m_indexCount = indices.size();
    m_vertexCount = vertices.size();
    m_warnedSizeMismatch = false;

    gl->glGenVertexArrays(1, &m_vao);
    gl->glGenBuffers(1, &m_vbo);
    gl->glGenBuffers(1, &m_ebo);

    gl->glBindVertexArray(m_vao);

    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * static_cast<int>(sizeof(Vertex)),
                     vertices.constData(), GL_DYNAMIC_DRAW);

    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * static_cast<int>(sizeof(uint32_t)),
                     indices.constData(), GL_STATIC_DRAW);

    // location 0: position (vec3)
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, pos)));

    // location 1: normal (vec3)
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, normal)));

    // location 2: uv (vec2)
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, uv)));

    gl->glBindVertexArray(0);
}

void GpuMesh::updateVertices(QOpenGLFunctions_3_3_Core *gl,
                             const QVector<Vertex> &vertices)
{
    if (!gl || !m_vbo)
        return;
    if (vertices.size() != m_vertexCount) {
        // A count mismatch means the caller's per-frame topology has
        // drifted from the upload-time topology. Surface it once per
        // mesh so the symptom ("the mesh stops animating") doesn't
        // masquerade as a different bug, but stay quiet thereafter so
        // a wedged mesh doesn't spam stderr at 60 Hz.
        if (!m_warnedSizeMismatch) {
            qWarning() << "GpuMesh::updateVertices count mismatch:"
                       << vertices.size() << "vs upload" << m_vertexCount
                       << "— suppressing further warnings for this mesh";
            m_warnedSizeMismatch = true;
        }
        return;
    }
    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
                        vertices.size() * static_cast<int>(sizeof(Vertex)),
                        vertices.constData());
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GpuMesh::draw(QOpenGLFunctions_3_3_Core *gl) const
{
    if (!m_vao || m_indexCount == 0)
        return;
    gl->glBindVertexArray(m_vao);
    gl->glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    gl->glBindVertexArray(0);
}

void GpuMesh::drawWireframe(QOpenGLFunctions_3_3_Core *gl) const
{
    if (!m_vao || m_indexCount == 0)
        return;
    gl->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    gl->glBindVertexArray(m_vao);
    gl->glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    gl->glBindVertexArray(0);
    gl->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void GpuMesh::destroy(QOpenGLFunctions_3_3_Core *gl)
{
    if (m_vao) { gl->glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { gl->glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { gl->glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    m_indexCount = 0;
    m_vertexCount = 0;
}
