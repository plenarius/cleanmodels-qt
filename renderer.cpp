#include "constants.h"
#include "renderer.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPair>
#include <QStack>
#include <cmath>
#include <numeric>

static const char *kVertexShader = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uModel, uView, uProjection;
uniform mat3 uNormalMatrix;

out vec3 vNormal, vWorldPos;
out vec2 vUV;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vUV = aUV;
    gl_Position = uProjection * uView * wp;
}
)glsl";

static const char *kFragmentShader = R"glsl(
#version 330 core
in vec3 vNormal, vWorldPos;
in vec2 vUV;

uniform vec3 uDiffuse, uAmbient, uSpecular;
uniform float uShininess;
uniform vec3 uLightDir, uLightColor, uViewPos;
uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing) normal = -normal;

    vec3 ambient = uAmbient * uLightColor * 0.3;

    float diff = max(dot(normal, -uLightDir), 0.0);
    vec3 diffuse = diff * uDiffuse * uLightColor;

    vec3 viewDir = normalize(uViewPos - vWorldPos);
    vec3 halfDir = normalize(-uLightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), max(uShininess, 1.0));
    vec3 specular = spec * uSpecular * uLightColor;

    vec3 result = ambient + diffuse + specular;

    vec4 baseColor = texture(uTexture, vUV);
    // Alpha-cutout: many NWN textures bake binary masks into the alpha
    // channel (lion mane sprites, fox tail tufts, foliage, etc.). Without
    // discarding the transparent fragments they still write to the depth
    // buffer and occlude the solid geometry behind them, producing
    // "gray blob" silhouettes around hairy/fuzzy parts.
    if (baseColor.a < 0.1) discard;
    FragColor = vec4(result * baseColor.rgb, baseColor.a);
}
)glsl";

GLuint Renderer::compileShader(QOpenGLFunctions_3_3_Core *gl, GLenum type, const char *src)
{
    GLuint shader = gl->glCreateShader(type);
    gl->glShaderSource(shader, 1, &src, nullptr);
    gl->glCompileShader(shader);

    GLint ok = 0;
    gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        gl->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        qWarning() << "Shader compile error:" << log;
        gl->glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint Renderer::linkProgram(QOpenGLFunctions_3_3_Core *gl, GLuint vert, GLuint frag)
{
    GLuint prog = gl->glCreateProgram();
    gl->glAttachShader(prog, vert);
    gl->glAttachShader(prog, frag);
    gl->glLinkProgram(prog);

    GLint ok = 0;
    gl->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        gl->glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        qWarning() << "Program link error:" << log;
        gl->glDeleteProgram(prog);
        return 0;
    }
    gl->glDeleteShader(vert);
    gl->glDeleteShader(frag);
    return prog;
}

void Renderer::initialize(QOpenGLFunctions_3_3_Core *gl)
{
    GLuint vs = compileShader(gl, GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader(gl, GL_FRAGMENT_SHADER, kFragmentShader);
    if (vs && fs)
        m_program = linkProgram(gl, vs, fs);

    if (m_program)
    {
        m_locModel = gl->glGetUniformLocation(m_program, "uModel");
        m_locView = gl->glGetUniformLocation(m_program, "uView");
        m_locProjection = gl->glGetUniformLocation(m_program, "uProjection");
        m_locNormalMatrix = gl->glGetUniformLocation(m_program, "uNormalMatrix");
        m_locDiffuse = gl->glGetUniformLocation(m_program, "uDiffuse");
        m_locAmbient = gl->glGetUniformLocation(m_program, "uAmbient");
        m_locSpecular = gl->glGetUniformLocation(m_program, "uSpecular");
        m_locShininess = gl->glGetUniformLocation(m_program, "uShininess");
        m_locLightDir = gl->glGetUniformLocation(m_program, "uLightDir");
        m_locLightColor = gl->glGetUniformLocation(m_program, "uLightColor");
        m_locViewPos = gl->glGetUniformLocation(m_program, "uViewPos");
        m_locTexture = gl->glGetUniformLocation(m_program, "uTexture");
    }

    unsigned char white[] = {255, 255, 255, 255};
    gl->glGenTextures(1, &m_whiteTex);
    gl->glBindTexture(GL_TEXTURE_2D, m_whiteTex);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, white);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glBindTexture(GL_TEXTURE_2D, 0);

    buildGrid(gl);
}

void Renderer::destroyRenderNodes(QOpenGLFunctions_3_3_Core *gl, std::vector<RenderNode> &nodes)
{
    for (auto &rn : nodes) {
        if (rn.mesh) rn.mesh->destroy(gl);
        if (rn.texture) rn.texture->destroy(gl);
    }
    nodes.clear();
}

void Renderer::shutdown(QOpenGLFunctions_3_3_Core *gl)
{
    destroyRenderNodes(gl, m_renderNodes);
    destroyRenderNodes(gl, m_referenceNodes);

    if (m_gridMesh) {
        m_gridMesh->destroy(gl);
        m_gridMesh.reset();
    }

    if (m_whiteTex) {
        gl->glDeleteTextures(1, &m_whiteTex);
        m_whiteTex = 0;
    }

    if (m_program)
    {
        gl->glDeleteProgram(m_program);
        m_program = 0;
    }
}

void Renderer::buildGrid(QOpenGLFunctions_3_3_Core *gl)
{
    // 10m x 10m ground grid at Z=0, 1m spacing (NWN uses 10 units = 1 tile = 10m)
    QVector<Vertex> verts;
    QVector<uint32_t> indices;
    const float extent = 5.0f; // +/- 5m
    const float step = 1.0f;

    auto addLine = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        Vertex v0{}, v1{};
        v0.pos[0] = x0; v0.pos[1] = y0; v0.pos[2] = z0;
        v0.normal[0] = 0; v0.normal[1] = 0; v0.normal[2] = 1;
        v1.pos[0] = x1; v1.pos[1] = y1; v1.pos[2] = z1;
        v1.normal[0] = 0; v1.normal[1] = 0; v1.normal[2] = 1;
        verts.append(v0);
        verts.append(v1);
        indices.append(base);
        indices.append(base + 1);
    };

    for (float v = -extent; v <= extent + 0.01f; v += step) {
        addLine(v, -extent, 0, v, extent, 0);
        addLine(-extent, v, 0, extent, v, 0);
    }

    m_gridMesh = std::make_unique<GpuMesh>();
    m_gridMesh->upload(gl, verts, indices);
}

void Renderer::prepareNodeList(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                               const QHash<int, QMatrix4x4> &boneWorld,
                               const QString &textureDir,
                               std::vector<RenderNode> &target)
{
    int root = scene.rootIndex();
    if (root < 0)
        return;

    // RAII swap m_textureDir for the duration of the upload so callers
    // can't leak a reference scene's texture directory into subsequent
    // main-scene operations (and vice versa). resolveTexturePath
    // inside uploadNodeMesh consults m_textureDir; once the upload
    // completes, RenderNode owns the GpuTexture handles directly and
    // m_textureDir is no longer needed for these nodes. The guard's
    // destructor restores the previous value even on exception unwind
    // (e.g. allocation failure deep in GpuMesh::upload), so a failed
    // upload can't quietly leave the renderer resolving every later
    // texture against the wrong directory.
    struct TexDirGuard {
        QString *target;
        QString saved;
        ~TexDirGuard() { *target = std::move(saved); }
        // Non-copyable / non-movable: a single scope owns the
        // restore. Two guards over the same target would both restore
        // on destruction and clobber each other; transferring
        // ownership across scopes would defeat the RAII contract.
        TexDirGuard(const TexDirGuard &) = delete;
        TexDirGuard &operator=(const TexDirGuard &) = delete;
        TexDirGuard(TexDirGuard &&) = delete;
        TexDirGuard &operator=(TexDirGuard &&) = delete;
    };
    TexDirGuard guard{&m_textureDir, m_textureDir};
    m_textureDir = textureDir;

    QMatrix4x4 identity;
    QSet<int> visited;
    buildRenderNodes(gl, scene, root, identity, boneWorld, target, visited);
}

void Renderer::prepareScene(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                            const QHash<int, QMatrix4x4> &boneWorld,
                            const QString &textureDir)
{
    destroyRenderNodes(gl, m_renderNodes);
    prepareNodeList(gl, scene, boneWorld, textureDir, m_renderNodes);
}

void Renderer::prepareReferenceModel(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                                      const QHash<int, QMatrix4x4> &boneWorld,
                                      const QString &textureDir)
{
    destroyRenderNodes(gl, m_referenceNodes);
    prepareNodeList(gl, scene, boneWorld, textureDir, m_referenceNodes);

    // Reference nodes are uploaded once and never re-animated. Clear
    // sceneNodeIdx so updateAnimatedMeshes (and any future code that
    // dereferences scene.nodes()[rn.sceneNodeIdx]) can't accidentally
    // index into the now-out-of-scope reference MdlScene.
    for (auto &rn : m_referenceNodes) {
        rn.sceneNodeIdx = -1;
        rn.isSkin = false;
    }

    m_showReference = true;
}

void Renderer::clearReferenceModel(QOpenGLFunctions_3_3_Core *gl)
{
    destroyRenderNodes(gl, m_referenceNodes);
    m_showReference = false;
}

void Renderer::buildRenderNodes(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                                int rootIdx, const QMatrix4x4 &rootParentWorld,
                                const QHash<int, QMatrix4x4> &boneWorld,
                                std::vector<RenderNode> &target,
                                QSet<int> &visited)
{
    // Iterative DFS on a heap-allocated worklist of (nodeIdx,
    // parentWorld) pairs. Recursion blew the stack on flat MDLs whose
    // parent-child graph chains thousands of nodes (each parented to
    // the previous): a cycle guard prevents infinite recursion but
    // does nothing about deep linear chains. Bounded now by heap
    // size, not thread stack.
    //
    // Trade-off: classic recursion peaks at O(depth) frames on the
    // native stack, while iterative-with-eager-child-push peaks at
    // O(depth + max_sibling_fanout) entries on the heap (each entry
    // is a `QPair<int, QMatrix4x4>`, ~72 bytes). For the typical NWN
    // creature (~50 nodes, fanout < 10) peak heap is sub-kilobyte;
    // for a pathological MDL with one parent of N children the
    // worklist briefly holds N entries. We accept that — the
    // alternative was leaving the stack-overflow DoS open. Eliminating
    // the wide-fanout cost would mean precomputing a flat preorder
    // visit array at parse time, which is a bigger refactor with no
    // current evidence of harm on real content.
    QStack<QPair<int, QMatrix4x4>> stack;
    stack.push({rootIdx, rootParentWorld});

    while (!stack.isEmpty()) {
        const auto [nodeIdx, parentWorld] = stack.pop();
        if (visited.contains(nodeIdx))
            continue;
        visited.insert(nodeIdx);

        const MdlNode &node = scene.nodes()[nodeIdx];

        // Prefer the animation player's world transform when available;
        // fall back to the parent-walk product for nodes the player
        // doesn't know about (e.g. when boneWorld is empty for tests).
        QMatrix4x4 world;
        auto bw = boneWorld.constFind(nodeIdx);
        if (bw != boneWorld.constEnd())
            world = bw.value();
        else
            world = parentWorld * makeLocalTransform(node);

        if (node.hasMesh() && node.render) {
            // CLEANMODELS_SKIN=0 falls back to drawing skin nodes the
            // same as trimeshes. Useful for A/B comparison against the
            // pre-skinning code.
            bool useSkinning = node.isSkin() && (qgetenv("CLEANMODELS_SKIN") != "0");

            if (useSkinning) {
                // Initial upload uses boneWorld (current animation
                // frame at load time). Per-frame animation updates
                // land in updateAnimatedMeshes() against sceneNodeIdx.
                const QVector<QVector3D> skinned = scene.skinnedVertsWith(nodeIdx, boneWorld);
                uploadNodeMesh(gl, node, QMatrix4x4(), target, &skinned, nodeIdx);
            } else {
                // Record sceneNodeIdx for non-skin meshes too so
                // updateAnimatedMeshes() can refresh their
                // worldTransform each frame and danglymesh tails
                // follow their parent bones.
                uploadNodeMesh(gl, node, world, target, nullptr, nodeIdx);
            }
        }

        // Push children in reverse so DFS visits in declaration order
        // (matches the previous recursive form's render order, which
        // affects translucent-blend ordering).
        const QVector<int> &children = scene.childrenOf(nodeIdx);
        for (int i = children.size() - 1; i >= 0; --i)
            stack.push({children[i], world});
    }
}

namespace {

// Build smooth per-vertex normals when authored normals are unavailable
// (or were invalidated by overriding the vertex positions, e.g. by CPU
// skinning). Lives at file scope so both the upload-time topology
// builder and the per-frame position recompute share one definition.
QVector<QVector3D> computeSmoothNormals(const MdlNode &node,
                                        const QVector<QVector3D> &verts)
{
    QVector<QVector3D> out;
    if (verts.isEmpty())
        return out;
    out.resize(verts.size(), QVector3D(0, 0, 0));
    for (const auto &face : node.faces) {
        // Reject negative indices as well as past-the-end ones. A
        // crafted MDL can author `face.verts[i] = -1`, which passes a
        // bare `>= verts.size()` check (e.g. -1 >= N is false) and
        // then becomes both an OOB read in `verts[-1]` and an OOB
        // write in `out[-1]` — the second is a genuine memory-
        // corruption primitive against Qt's heap. The expanded-vert
        // path (appendExpandedVerts) already guards both bounds; this
        // site is matched to the same contract.
        if (face.verts[0] < 0 || face.verts[0] >= verts.size() ||
            face.verts[1] < 0 || face.verts[1] >= verts.size() ||
            face.verts[2] < 0 || face.verts[2] >= verts.size())
            continue;
        QVector3D e1 = verts[face.verts[1]] - verts[face.verts[0]];
        QVector3D e2 = verts[face.verts[2]] - verts[face.verts[0]];
        QVector3D fn = QVector3D::crossProduct(e1, e2);
        out[face.verts[0]] += fn;
        out[face.verts[1]] += fn;
        out[face.verts[2]] += fn;
    }
    for (auto &n : out)
        n.normalize();
    return out;
}

// Shared per-face vertex builder for both buildUploadMesh and
// rebuildExpandedVerts. Splits responsibility cleanly: callers decide
// whether they also need a fresh index buffer, this function is purely
// "give me the per-face Vertex stream".
void appendExpandedVerts(const MdlNode &node,
                         const QVector<QVector3D> *vertsOverride,
                         QVector<Vertex> &vertices)
{
    vertices.clear();

    const QVector<QVector3D> &verts = vertsOverride ? *vertsOverride : node.verts;

    // Authored normals are only trustworthy when the vertex positions
    // they were exported against are still in use. With vertsOverride
    // we've already morphed the mesh (CPU skinning, danglymesh, etc.);
    // the authored normals would be referencing the bind-pose
    // positions, so we throw them out and recompute from the new
    // verts. Without an override the per-vertex count match is the
    // authoring tool's signal that authored normals belong to these
    // verts.
    bool hasNormals = !vertsOverride && (node.normals.size() == verts.size());
    QVector<QVector3D> smoothNormals = hasNormals
        ? QVector<QVector3D>{}
        : computeSmoothNormals(node, verts);

    vertices.reserve(static_cast<int>(node.faces.size()) * 3);

    for (const auto &face : node.faces)
    {
        for (int i = 0; i < 3; ++i)
        {
            Vertex v{};
            int vi = face.verts[i];
            if (vi >= 0 && vi < verts.size())
            {
                const auto &p = verts[vi];
                v.pos[0] = p.x(); v.pos[1] = p.y(); v.pos[2] = p.z();
            }

            if (hasNormals && vi >= 0 && vi < node.normals.size())
            {
                const auto &n = node.normals[vi];
                v.normal[0] = n.x(); v.normal[1] = n.y(); v.normal[2] = n.z();
            }
            else if (!smoothNormals.isEmpty() && vi >= 0 && vi < smoothNormals.size())
            {
                const auto &n = smoothNormals[vi];
                v.normal[0] = n.x(); v.normal[1] = n.y(); v.normal[2] = n.z();
            }
            else
            {
                v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 1;
            }

            int ui = face.uvs[i];
            if (ui >= 0 && ui < node.tverts.size())
            {
                const auto &t = node.tverts[ui];
                v.uv[0] = t.x();
                v.uv[1] = t.y();
            }

            vertices.append(v);
        }
    }
}

} // namespace

void Renderer::buildUploadMesh(const MdlNode &node,
                               const QVector<QVector3D> *vertsOverride,
                               QVector<Vertex> &vertices,
                               QVector<uint32_t> &indices)
{
    appendExpandedVerts(node, vertsOverride, vertices);
    indices.resize(vertices.size());
    // Sequential 0..N-1: every face-corner vertex is unique because we
    // emit them one-per-corner above (no welding by position/UV).
    std::iota(indices.begin(), indices.end(), uint32_t{0});
}

void Renderer::rebuildExpandedVerts(const MdlNode &node,
                                    const QVector<QVector3D> &verts,
                                    QVector<Vertex> &vertices)
{
    appendExpandedVerts(node, &verts, vertices);
}

void Renderer::uploadNodeMesh(QOpenGLFunctions_3_3_Core *gl, const MdlNode &node,
                              const QMatrix4x4 &worldTransform,
                              std::vector<RenderNode> &target,
                              const QVector<QVector3D> *vertsOverride,
                              int sceneNodeIdx)
{
    QVector<Vertex> vertices;
    QVector<uint32_t> indices;
    buildUploadMesh(node, vertsOverride, vertices, indices);
    if (vertices.isEmpty())
        return;

    RenderNode rn;
    rn.worldTransform = worldTransform;
    rn.ambient = node.ambient;
    rn.diffuse = node.diffuse;
    rn.specular = node.specular;
    rn.shininess = node.shininess;
    rn.sceneNodeIdx = sceneNodeIdx;
    rn.isSkin = (node.isSkin() && vertsOverride != nullptr);
    rn.mesh = std::make_unique<GpuMesh>();
    rn.mesh->upload(gl, vertices, indices);

    if (!node.bitmap.isEmpty() && node.bitmap.toLower() != "null") {
        QString texPath = resolveTexturePath(node.bitmap);
        if (!texPath.isEmpty()) {
            auto tex = std::make_unique<GpuTexture>();
            if (tex->loadFromFile(gl, texPath))
                rn.texture = std::move(tex);
            else
                qWarning() << "Failed to load texture:" << texPath;
        }
    }

    target.push_back(std::move(rn));
}

void Renderer::updateAnimatedMeshes(QOpenGLFunctions_3_3_Core *gl,
                                    const MdlScene &scene,
                                    const QHash<int, QMatrix4x4> &boneWorld)
{
    if (!gl)
        return;
    const QVector<MdlNode> &sceneNodes = scene.nodes();
    QVector<Vertex> vertices;
    for (auto &rn : m_renderNodes) {
        if (rn.sceneNodeIdx < 0 || rn.sceneNodeIdx >= sceneNodes.size() || !rn.mesh)
            continue;

        const MdlNode &node = sceneNodes[rn.sceneNodeIdx];

        if (rn.isSkin) {
            // Skin meshes: re-skin verts in place. Their world transform
            // is identity (verts live in model space after skinning).
            const QVector<QVector3D> skinned =
                scene.skinnedVertsWith(rn.sceneNodeIdx, boneWorld);
            if (skinned.isEmpty())
                continue;
            // Per-frame fast path: rebuilds verts only — index buffer
            // already lives on the GPU from upload time.
            rebuildExpandedVerts(node, skinned, vertices);
            if (vertices.isEmpty())
                continue;
            rn.mesh->updateVertices(gl, vertices);
        } else {
            // Non-skin meshes (trimesh / danglymesh / aabb): keep the
            // mesh data static and just refresh the world transform from
            // the animation player's per-bone matrices. Without this,
            // separate parts like the squirrel's danglymesh tail stay
            // pinned to their bind-pose world position even when their
            // parent bone is animating.
            auto it = boneWorld.constFind(rn.sceneNodeIdx);
            if (it != boneWorld.constEnd())
                rn.worldTransform = it.value();
        }
    }
}

QString Renderer::resolveTexturePath(const QString &bitmap) const
{
    if (m_textureDir.isEmpty())
        return {};

    QDir dir(m_textureDir);
    static const QStringList extensions = {"tga", "dds", "png", "bmp", "jpg", "jpeg"};

    for (const auto &ext : extensions) {
        QString candidate = dir.filePath(bitmap + "." + ext);
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    QStringList entries = dir.entryList(QDir::Files);
    QString lowerBitmap = bitmap.toLower();
    for (const auto &entry : entries) {
        QString baseName = QFileInfo(entry).completeBaseName().toLower();
        if (baseName == lowerBitmap) {
            QString suffix = QFileInfo(entry).suffix().toLower();
            if (extensions.contains(suffix))
                return dir.filePath(entry);
        }
    }

    return {};
}

void Renderer::renderNodes(QOpenGLFunctions_3_3_Core *gl,
                           const std::vector<RenderNode> &nodes, bool wireframe,
                           const MaterialOverride &matOverride)
{
    for (const auto &rn : nodes)
    {
        gl->glUniformMatrix4fv(m_locModel, 1, GL_FALSE, rn.worldTransform.constData());

        QMatrix3x3 normalMat = rn.worldTransform.normalMatrix();
        gl->glUniformMatrix3fv(m_locNormalMatrix, 1, GL_FALSE, normalMat.constData());

        if (matOverride.active) {
            gl->glUniform3f(m_locDiffuse, matOverride.diffuse.x(), matOverride.diffuse.y(), matOverride.diffuse.z());
            gl->glUniform3f(m_locAmbient, matOverride.ambient.x(), matOverride.ambient.y(), matOverride.ambient.z());
            gl->glUniform3f(m_locSpecular, 0.0f, 0.0f, 0.0f);
            gl->glUniform1f(m_locShininess, 1.0f);
        } else {
            gl->glUniform3f(m_locDiffuse, rn.diffuse.x(), rn.diffuse.y(), rn.diffuse.z());
            gl->glUniform3f(m_locAmbient, rn.ambient.x(), rn.ambient.y(), rn.ambient.z());
            gl->glUniform3f(m_locSpecular, rn.specular.x(), rn.specular.y(), rn.specular.z());
            gl->glUniform1f(m_locShininess, rn.shininess);
        }

        if (!wireframe && !matOverride.active && rn.texture && rn.texture->isValid())
            rn.texture->bind(gl, 0);
        else {
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, m_whiteTex);
        }
        gl->glUniform1i(m_locTexture, 0);

        if (wireframe)
            rn.mesh->drawWireframe(gl);
        else
            rn.mesh->draw(gl);
    }
}

void Renderer::render(QOpenGLFunctions_3_3_Core *gl, const Camera &camera)
{
    gl->glClearColor(ViewportColor::BgR, ViewportColor::BgG, ViewportColor::BgB, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_program)
        return;

    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LESS);
    gl->glEnable(GL_CULL_FACE);
    gl->glCullFace(GL_BACK);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->glEnable(GL_MULTISAMPLE);

    gl->glUseProgram(m_program);

    QMatrix4x4 view = camera.viewMatrix();
    QMatrix4x4 proj = camera.projectionMatrix();
    QVector3D camPos = camera.position();

    gl->glUniformMatrix4fv(m_locView, 1, GL_FALSE, view.constData());
    gl->glUniformMatrix4fv(m_locProjection, 1, GL_FALSE, proj.constData());
    gl->glUniform3f(m_locViewPos, camPos.x(), camPos.y(), camPos.z());

    QVector3D lightDir = QVector3D(-0.5f, -1.0f, -0.5f).normalized();
    gl->glUniform3f(m_locLightDir, lightDir.x(), lightDir.y(), lightDir.z());
    gl->glUniform3f(m_locLightColor, 1.0f, 1.0f, 1.0f);

    // Ground grid
    if (m_showGrid && m_gridMesh && m_gridMesh->isValid())
    {
        QMatrix4x4 identity;
        gl->glUniformMatrix4fv(m_locModel, 1, GL_FALSE, identity.constData());
        QMatrix3x3 normalMat = identity.normalMatrix();
        gl->glUniformMatrix3fv(m_locNormalMatrix, 1, GL_FALSE, normalMat.constData());

        gl->glUniform3f(m_locDiffuse, ViewportColor::GridR, ViewportColor::GridG, ViewportColor::GridB);
        gl->glUniform3f(m_locAmbient, ViewportColor::GridR, ViewportColor::GridG, ViewportColor::GridB);
        gl->glUniform3f(m_locSpecular, 0.0f, 0.0f, 0.0f);
        gl->glUniform1f(m_locShininess, 1.0f);

        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, m_whiteTex);
        gl->glUniform1i(m_locTexture, 0);

        gl->glBindVertexArray(m_gridMesh->vao());
        gl->glDrawElements(GL_LINES, m_gridMesh->indexCount(), GL_UNSIGNED_INT, nullptr);
        gl->glBindVertexArray(0);
    }

    if (m_showReference && !m_referenceNodes.empty())
    {
        gl->glDisable(GL_CULL_FACE);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->glDepthMask(GL_FALSE);

        MaterialOverride refMat;
        refMat.active = true;
        refMat.diffuse = {ViewportColor::RefR, ViewportColor::RefG, ViewportColor::RefB};
        refMat.ambient = refMat.diffuse;
        renderNodes(gl, m_referenceNodes, true, refMat);

        gl->glDepthMask(GL_TRUE);
        gl->glEnable(GL_CULL_FACE);
    }

    // Main scene
    if (!m_renderNodes.empty())
    {
        if (m_wireframe)
            gl->glDisable(GL_CULL_FACE);
        renderNodes(gl, m_renderNodes, m_wireframe);
        if (m_wireframe)
            gl->glEnable(GL_CULL_FACE);
    }

    gl->glUseProgram(0);
}
