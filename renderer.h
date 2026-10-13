#ifndef RENDERER_H
#define RENDERER_H

#include "camera.h"
#include "gpumesh.h"
#include "gputexture.h"
#include "mdlscene.h"
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QSet>
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

    // Build the primary render-node list from `scene`. `boneWorld` supplies
    // the world transform for every node, keyed by node index in
    // MdlScene::nodes() (typically from a freshly-set-up
    // MdlAnimationPlayer at its current frame); skin meshes are skinned
    // against it, non-skin meshes use it for their world transform. Empty
    // hash falls back to bind-pose world transforms.
    void prepareScene(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                      const QHash<int, QMatrix4x4> &boneWorld = {},
                      const QString &textureDir = QString());
    // Same as prepareScene but populates the reference (overlay) node list.
    // Reference nodes are uploaded once and never re-animated, so they don't
    // retain a sceneNodeIdx — the sourcing MdlScene can safely go out of
    // scope after this call returns.
    void prepareReferenceModel(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                               const QHash<int, QMatrix4x4> &boneWorld = {},
                               const QString &textureDir = QString());
    void clearReferenceModel(QOpenGLFunctions_3_3_Core *gl);
    void render(QOpenGLFunctions_3_3_Core *gl, const Camera &camera);

    // Re-skin every primary RenderNode marked isSkin against the supplied
    // scene + bone-world map (typically driven by an MdlAnimationPlayer)
    // and rewrite its VBO contents. Non-skin meshes get their world
    // transform refreshed from `boneWorld[sceneNodeIdx]` so danglymesh
    // tails and other separate parts follow their animated parent bones.
    // Topology and textures are preserved.
    void updateAnimatedMeshes(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                              const QHash<int, QMatrix4x4> &boneWorld);

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

    // Shared prologue for prepareScene / prepareReferenceModel: sets
    // m_textureDir for the duration of the upload (so uploadNodeMesh's
    // resolveTexturePath finds the right textures), resolves the scene
    // root, and dispatches buildRenderNodes with a fresh visited set,
    // restoring the previous m_textureDir on exit. Encapsulating the
    // save/restore here means callers can't forget to restore it,
    // which previously meant prepareReferenceModel had a brittle
    // open-coded dance that would corrupt the main scene's
    // texture-resolution context if it threw or returned early.
    void prepareNodeList(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                         const QHash<int, QMatrix4x4> &boneWorld,
                         const QString &textureDir,
                         std::vector<RenderNode> &target);
    // Iterative DFS over the scene's child-adjacency graph
    // (childrenOf) rooted at `rootIdx`. `visited` short-circuits
    // cycles in that graph: a malformed MDL whose parent-name links
    // form a loop (two nodes claiming each other as parent, or any
    // longer cycle) would otherwise re-push the same node indices
    // onto the worklist forever and grow it without bound. The
    // explicit worklist also keeps deep linear chains bounded by
    // heap size instead of thread stack size. `visited` is threaded
    // through rather than held as state so reference + main scenes
    // keep independent visit sets.
    void buildRenderNodes(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                          int rootIdx, const QMatrix4x4 &rootParentWorld,
                          const QHash<int, QMatrix4x4> &boneWorld,
                          std::vector<RenderNode> &target,
                          QSet<int> &visited);
    // When `vertsOverride` is non-null, those positions are used instead of
    // node.verts and authored normals are discarded (face normals get
    // regenerated to match the overridden geometry). Used for skin nodes,
    // where the verts come from CPU skinning in model space.
    void uploadNodeMesh(QOpenGLFunctions_3_3_Core *gl, const MdlNode &node,
                        const QMatrix4x4 &worldTransform,
                        std::vector<RenderNode> &target,
                        const QVector<QVector3D> *vertsOverride = nullptr,
                        int sceneNodeIdx = -1);
    // Initial-upload path: per-face triangle-corner Vertex entries plus
    // a sequential index buffer [0, 1, ..., 3F-1]. With `vertsOverride`
    // non-null we treat those positions as authoritative and regenerate
    // smooth normals from them (authored normals only line up with the
    // authored verts they were exported with).
    void buildUploadMesh(const MdlNode &node,
                         const QVector<QVector3D> *vertsOverride,
                         QVector<Vertex> &outVerts,
                         QVector<uint32_t> &outIndices);
    // Per-frame fast path: rebuilds vertices only — UVs, positions, and
    // normals — for an already-uploaded mesh whose index buffer (a
    // sequential 0..3F-1 from the upload) is unchanged. Always called
    // with vertsOverride from CPU skinning, so smooth normals get
    // regenerated to match the new positions.
    void rebuildExpandedVerts(const MdlNode &node,
                              const QVector<QVector3D> &verts,
                              QVector<Vertex> &outVerts);
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
