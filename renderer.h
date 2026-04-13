#ifndef RENDERER_H
#define RENDERER_H

#include "camera.h"
#include "gpumesh.h"
#include "gputexture.h"
#include "mdlscene.h"
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QVector3D>
#include <memory>
#include <vector>

struct RenderNode {
    std::unique_ptr<GpuMesh> mesh;
    std::unique_ptr<GpuTexture> texture;
    QMatrix4x4 worldTransform;
    QVector3D ambient;
    QVector3D diffuse;
    QVector3D specular;
    float shininess = 1.0f;
};

class Renderer
{
public:
    Renderer() = default;

    void initialize(QOpenGLFunctions_3_3_Core *gl);
    void shutdown(QOpenGLFunctions_3_3_Core *gl);

    void prepareScene(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                      const QString &textureDir = QString());
    void render(QOpenGLFunctions_3_3_Core *gl, const Camera &camera);

    GLuint program() const { return m_program; }
    int renderNodeCount() const { return static_cast<int>(m_renderNodes.size()); }

private:
    GLuint m_program = 0;
    GLint m_locModel = -1;
    GLint m_locView = -1;
    GLint m_locProjection = -1;
    GLint m_locNormalMatrix = -1;
    GLint m_locDiffuse = -1;
    GLint m_locAmbient = -1;
    GLint m_locSpecular = -1;
    GLint m_locShininess = -1;
    GLint m_locLightDir = -1;
    GLint m_locLightColor = -1;
    GLint m_locViewPos = -1;
    GLint m_locTexture = -1;

    GLuint m_whiteTex = 0;
    std::vector<RenderNode> m_renderNodes;
    QString m_textureDir;

    GLuint compileShader(QOpenGLFunctions_3_3_Core *gl, GLenum type, const char *src);
    GLuint linkProgram(QOpenGLFunctions_3_3_Core *gl, GLuint vert, GLuint frag);

    void buildRenderNodes(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                          int nodeIdx, const QMatrix4x4 &parentWorld);
    void uploadNodeMesh(QOpenGLFunctions_3_3_Core *gl, const MdlNode &node,
                        const QMatrix4x4 &worldTransform);

    QString resolveTexturePath(const QString &bitmap) const;
};

#endif
