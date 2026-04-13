#include "renderer.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

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

    // Ambient
    vec3 ambient = uAmbient * uLightColor * 0.3;

    // Diffuse (Lambert)
    float diff = max(dot(normal, -uLightDir), 0.0);
    vec3 diffuse = diff * uDiffuse * uLightColor;

    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    vec3 halfDir = normalize(-uLightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), max(uShininess, 1.0));
    vec3 specular = spec * uSpecular * uLightColor;

    vec3 result = ambient + diffuse + specular;

    // Texture as color map, multiplied onto lit result
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

    // 1x1 white fallback so the sampler is always valid
    unsigned char white[] = {255, 255, 255, 255};
    gl->glGenTextures(1, &m_whiteTex);
    gl->glBindTexture(GL_TEXTURE_2D, m_whiteTex);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, white);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::shutdown(QOpenGLFunctions_3_3_Core *gl)
{
    for (auto &rn : m_renderNodes) {
        if (rn.mesh) rn.mesh->destroy(gl);
        if (rn.texture) rn.texture->destroy(gl);
    }
    m_renderNodes.clear();

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

void Renderer::prepareScene(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                            const QString &textureDir)
{
    for (auto &rn : m_renderNodes) {
        if (rn.mesh) rn.mesh->destroy(gl);
        if (rn.texture) rn.texture->destroy(gl);
    }
    m_renderNodes.clear();
    m_textureDir = textureDir;

    int root = scene.rootIndex();
    if (root < 0)
        return;

    QMatrix4x4 identity;
    buildRenderNodes(gl, scene, root, identity);
}

void Renderer::buildRenderNodes(QOpenGLFunctions_3_3_Core *gl, const MdlScene &scene,
                                int nodeIdx, const QMatrix4x4 &parentWorld)
{
    const MdlNode &node = scene.nodes()[nodeIdx];

    QMatrix4x4 local;
    local.translate(node.position);
    local.rotate(node.orientation);
    if (node.scale != 1.0f)
        local.scale(node.scale);

    QMatrix4x4 world = parentWorld * local;

    if (node.hasMesh() && node.render)
        uploadNodeMesh(gl, node, world);

    for (int childIdx : scene.childrenOf(nodeIdx))
        buildRenderNodes(gl, scene, childIdx, world);
}

void Renderer::uploadNodeMesh(QOpenGLFunctions_3_3_Core *gl, const MdlNode &node,
                              const QMatrix4x4 &worldTransform)
{
    QVector<Vertex> vertices;
    QVector<uint32_t> indices;

    bool hasNormals = (node.normals.size() == node.verts.size());

    // Compute smooth vertex normals by averaging face normals per vertex
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
            // Weight by face area (unnormalized cross product magnitude)
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

    m_renderNodes.push_back(std::move(rn));
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

    // Also check case-insensitive by scanning directory
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

void Renderer::render(QOpenGLFunctions_3_3_Core *gl, const Camera &camera)
{
    gl->glClearColor(0.18f, 0.20f, 0.25f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_program || m_renderNodes.empty()) {
        static bool logged = false;
        if (!logged) {
            qDebug() << "RENDERER: render early return, program:" << m_program
                     << "nodes:" << m_renderNodes.size();
            logged = true;
        }
        return;
    }

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

    for (const auto &rn : m_renderNodes)
    {
        gl->glUniformMatrix4fv(m_locModel, 1, GL_FALSE, rn.worldTransform.constData());

        QMatrix3x3 normalMat = rn.worldTransform.normalMatrix();
        gl->glUniformMatrix3fv(m_locNormalMatrix, 1, GL_FALSE, normalMat.constData());

        gl->glUniform3f(m_locDiffuse, rn.diffuse.x(), rn.diffuse.y(), rn.diffuse.z());
        gl->glUniform3f(m_locAmbient, rn.ambient.x(), rn.ambient.y(), rn.ambient.z());
        gl->glUniform3f(m_locSpecular, rn.specular.x(), rn.specular.y(), rn.specular.z());
        gl->glUniform1f(m_locShininess, rn.shininess);

        if (rn.texture && rn.texture->isValid())
            rn.texture->bind(gl, 0);
        else {
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, m_whiteTex);
        }
        gl->glUniform1i(m_locTexture, 0);

        rn.mesh->draw(gl);
    }

    gl->glUseProgram(0);
}
