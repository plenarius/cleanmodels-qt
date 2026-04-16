#include "constants.h"
#include "renderer.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <cmath>

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

void Renderer::prepareScene(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                            const QString &textureDir)
{
    destroyRenderNodes(gl, m_renderNodes);
    m_textureDir = textureDir;

    int root = scene.rootIndex();
    if (root < 0)
        return;

    QMatrix4x4 identity;
    buildRenderNodes(gl, scene, root, identity, m_renderNodes);
}

void Renderer::prepareReferenceModel(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                                      const QString &textureDir)
{
    destroyRenderNodes(gl, m_referenceNodes);

    QString savedTexDir = m_textureDir;
    m_textureDir = textureDir;

    int root = scene.rootIndex();
    if (root >= 0) {
        QMatrix4x4 identity;
        buildRenderNodes(gl, scene, root, identity, m_referenceNodes);
    }

    m_textureDir = savedTexDir;
    m_showReference = true;
}

void Renderer::clearReferenceModel(QOpenGLFunctions_3_3_Core *gl)
{
    destroyRenderNodes(gl, m_referenceNodes);
    m_showReference = false;
}

void Renderer::buildRenderNodes(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                                int nodeIdx, const QMatrix4x4 &parentWorld,
                                std::vector<RenderNode> &target)
{
    const MdlNode &node = scene.nodes()[nodeIdx];

    QMatrix4x4 local;
    local.translate(node.position);
    local.rotate(node.orientation);
    if (node.scale != 1.0f)
        local.scale(node.scale);

    QMatrix4x4 world = parentWorld * local;

    if (node.hasMesh() && node.render)
        uploadNodeMesh(gl, node, world, target);

    for (int childIdx : scene.childrenOf(nodeIdx))
        buildRenderNodes(gl, scene, childIdx, world, target);
}

void Renderer::uploadNodeMesh(QOpenGLFunctions_3_3_Core *gl, const MdlNode &node,
                              const QMatrix4x4 &worldTransform,
                              std::vector<RenderNode> &target)
{
    QVector<Vertex> vertices;
    QVector<uint32_t> indices;

    bool hasNormals = (node.normals.size() == node.verts.size());

    QVector<QVector3D> smoothNormals;
    if (!hasNormals && !node.verts.isEmpty()) {
        smoothNormals.resize(node.verts.size(), QVector3D(0, 0, 0));
        for (const auto &face : node.faces) {
            if (face.verts[0] >= node.verts.size() ||
                face.verts[1] >= node.verts.size() ||
                face.verts[2] >= node.verts.size())
                continue;

            QVector3D e1 = node.verts[face.verts[1]] - node.verts[face.verts[0]];
            QVector3D e2 = node.verts[face.verts[2]] - node.verts[face.verts[0]];
            QVector3D fn = QVector3D::crossProduct(e1, e2);
            smoothNormals[face.verts[0]] += fn;
            smoothNormals[face.verts[1]] += fn;
            smoothNormals[face.verts[2]] += fn;
        }
        for (auto &n : smoothNormals)
            n.normalize();
    }

    for (const auto &face : node.faces)
    {
        for (int i = 0; i < 3; ++i)
        {
            Vertex v{};
            int vi = face.verts[i];
            if (vi >= 0 && vi < node.verts.size())
            {
                const auto &p = node.verts[vi];
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
                v.uv[0] = t.x(); v.uv[1] = t.y();
            }

            indices.append(static_cast<uint32_t>(vertices.size()));
            vertices.append(v);
        }
    }

    if (vertices.isEmpty())
        return;

    RenderNode rn;
    rn.worldTransform = worldTransform;
    rn.ambient = node.ambient;
    rn.diffuse = node.diffuse;
    rn.specular = node.specular;
    rn.shininess = node.shininess;
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
