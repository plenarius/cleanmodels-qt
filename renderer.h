#ifndef RENDERER_H
#define RENDERER_H

#include "camera.h"
#include "gpumesh.h"
#include "gputexture.h"
#include "mdlscene.h"
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QString>
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

    // Animation hookup. For skin meshes, sceneNodeIdx points at the source
    // MdlNode so the renderer can re-skin per frame using new bone matrices
    // without re-uploading the texture or rebuilding the topology. -1 means
    // "static — never animated".
    int sceneNodeIdx = -1;
    bool isSkin = false;
};

class Renderer
{
public:
    Renderer() = default;

    void initialize(QOpenGLFunctions_3_3_Core *gl);
    void shutdown(QOpenGLFunctions_3_3_Core *gl);

    void prepareScene(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                      const QString &textureDir = QString());
    void prepareReferenceModel(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                               const QString &textureDir = QString());
    void clearReferenceModel(QOpenGLFunctions_3_3_Core *gl);
    void render(QOpenGLFunctions_3_3_Core *gl, const Camera &camera);

    // Re-skin every primary RenderNode marked isSkin against the supplied
    // scene + bone-world map (typically driven by an MdlAnimationPlayer)
    // and rewrite its VBO contents. Topology and textures are preserved.
    // No-op for nodes whose isSkin is false.
    void updateAnimatedMeshes(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                              const QHash<QString, QMatrix4x4> &boneWorld);

    void setWireframe(bool on) { m_wireframe = on; }
    bool wireframe() const { return m_wireframe; }
    bool showReference() const { return m_showReference; }
    void setShowGrid(bool on) { m_showGrid = on; }
    bool showGrid() const { return m_showGrid; }

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
    bool m_wireframe = false;
    bool m_showReference = false;
    bool m_showGrid = true;
    std::vector<RenderNode> m_renderNodes;
    std::vector<RenderNode> m_referenceNodes;
    std::unique_ptr<GpuMesh> m_gridMesh;
    QString m_textureDir;

    GLuint compileShader(QOpenGLFunctions_3_3_Core *gl, GLenum type, const char *src);
    GLuint linkProgram(QOpenGLFunctions_3_3_Core *gl, GLuint vert, GLuint frag);
    void destroyRenderNodes(QOpenGLFunctions_3_3_Core *gl, std::vector<RenderNode> &nodes);

    void buildRenderNodes(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                          int nodeIdx, const QMatrix4x4 &parentWorld,
                          std::vector<RenderNode> &target);
    // When `vertsOverride` is non-null, those positions are used instead of
    // node.verts and authored normals are discarded (face normals get
    // regenerated to match the overridden geometry). Used for skin nodes,
    // where the verts come from CPU skinning in model space.
    void uploadNodeMesh(QOpenGLFunctions_3_3_Core *gl, const MdlNode &node,
                        const QMatrix4x4 &worldTransform,
                        std::vector<RenderNode> &target,
                        const QVector<QVector3D> *vertsOverride = nullptr,
                        int sceneNodeIdx = -1);
    // Build the (already expanded, per-face-corner) Vertex array for a node.
    // Shared by initial upload and per-frame re-skin updates, so the two
    // paths stay structurally in sync.
    void buildExpandedVertices(const MdlNode &node,
                               const QVector<QVector3D> *vertsOverride,
                               QVector<Vertex> &outVerts,
                               QVector<uint32_t> &outIndices);
    struct MaterialOverride {
        QVector3D diffuse, ambient;
        bool active;
    };
    void renderNodes(QOpenGLFunctions_3_3_Core *gl, const std::vector<RenderNode> &nodes,
                     bool wireframe, const MaterialOverride &matOverride = {QVector3D(), QVector3D(), false});
    void buildGrid(QOpenGLFunctions_3_3_Core *gl);

    QString resolveTexturePath(const QString &bitmap) const;
};

#endif
