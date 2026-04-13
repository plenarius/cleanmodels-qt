#include "gpumesh.h"
#include <utility>

GpuMesh::~GpuMesh() = default;

GpuMesh::GpuMesh(GpuMesh &&other) noexcept
    : m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo), m_indexCount(other.m_indexCount)
{
    other.m_vao = other.m_vbo = other.m_ebo = 0;
    other.m_indexCount = 0;
}

GpuMesh &GpuMesh::operator=(GpuMesh &&other) noexcept
{
    if (this != &other)
    {
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_indexCount = other.m_indexCount;
        other.m_vao = other.m_vbo = other.m_ebo = 0;
        other.m_indexCount = 0;
    }
    return *this;
}

void GpuMesh::upload(QOpenGLFunctions_3_3_Core *gl,
                     const QVector<Vertex> &vertices,
                     const QVector<uint32_t> &indices)
{
    m_indexCount = indices.size();

    gl->glGenVertexArrays(1, &m_vao);
    gl->glGenBuffers(1, &m_vbo);
    gl->glGenBuffers(1, &m_ebo);

    gl->glBindVertexArray(m_vao);

    gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl->glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * static_cast<int>(sizeof(Vertex)),
                     vertices.constData(), GL_STATIC_DRAW);

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
}
