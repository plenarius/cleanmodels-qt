#include "constants.h"
#include "modelviewport.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>

ModelViewport::ModelViewport(QWidget *parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt;
    fmt.setVersion(GLDefaults::MajorVersion, GLDefaults::MinorVersion);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(GLDefaults::DepthBits);
    fmt.setSamples(GLDefaults::Samples);
    setFormat(fmt);

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

ModelViewport::~ModelViewport()
{
    if (!m_initialized)
        return;
    makeCurrent();
    m_renderer.shutdown(this);
    doneCurrent();
}

void ModelViewport::setCliBinaryPath(const QString &path)
{
    m_cliBinaryPath = path;
}

void ModelViewport::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        qWarning() << "VIEWPORT: Failed to initialize OpenGL 3.3 Core functions";
        return;
    }

    m_renderer.initialize(this);
    m_initialized = true;

    glViewport(0, 0, width(), height());
    m_camera.setAspectRatio(static_cast<float>(width()) / static_cast<float>(std::max(height(), 1)));
}

void ModelViewport::resizeGL(int w, int h)
{
    if (h == 0) h = 1;
    m_camera.setAspectRatio(static_cast<float>(w) / static_cast<float>(h));
    if (m_initialized)
        glViewport(0, 0, w, h);
}

void ModelViewport::paintGL()
{
    if (!m_initialized)
        return;
    m_renderer.render(this, m_camera);
}

static bool fileIsBinary(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QByteArray hdr = f.read(4);
    f.close();
    return hdr.size() == 4 && hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 0 && hdr[3] == 0;
}

QString ModelViewport::readMdlToAscii(const QString &mdlPath, QString *errorOut)
{
    if (fileIsBinary(mdlPath))
    {
        if (m_cliBinaryPath.isEmpty()) {
            if (errorOut) *errorOut = "CLI binary path not set";
            return {};
        }

        QProcess proc;
        proc.setProgram(m_cliBinaryPath);
        proc.setArguments({CliFlag::DecompileOnly, mdlPath});
        proc.start(QIODevice::ReadOnly);

        if (!proc.waitForFinished(CliDefaults::ProcessTimeoutMs)) {
            if (errorOut) *errorOut = "CLI timed out decompiling " + mdlPath;
            return {};
        }

        if (proc.exitCode() != 0) {
            if (errorOut) *errorOut = "CLI error: " + proc.readAllStandardError();
            return {};
        }

        return QString::fromUtf8(proc.readAllStandardOutput());
    }

    QFile f(mdlPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = "Cannot open " + mdlPath;
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

void ModelViewport::previewFile(const QString &mdlPath)
{
    if (!m_initialized) {
        emit previewError("Viewport not initialized");
        return;
    }

    QString error;
    QString ascii = readMdlToAscii(mdlPath, &error);
    if (ascii.isEmpty()) {
        emit previewError(error.isEmpty() ? "Empty MDL content for " + mdlPath : error);
        return;
    }

    loadModel(ascii, QFileInfo(mdlPath).absolutePath());
}

void ModelViewport::loadModel(const QString &asciiMdl, const QString &textureDir)
{
    if (!m_initialized)
        return;

    MdlScene scene;
    if (!scene.loadFromString(asciiMdl)) {
        emit previewError("Failed to parse MDL scene");
        return;
    }

    makeCurrent();
    m_renderer.prepareScene(this, scene, textureDir);
    doneCurrent();

    QVector3D bmin, bmax;
    scene.computeBounds(bmin, bmax);
    m_camera.focusOnBounds(bmin, bmax);

    m_hasModel = true;
    update();
    emit modelLoaded("model");
}

void ModelViewport::clearModel()
{
    if (!m_initialized)
        return;
    makeCurrent();
    m_renderer.prepareScene(this, MdlScene());
    doneCurrent();
    m_hasModel = false;
    update();
}

void ModelViewport::setWireframe(bool on)
{
    m_renderer.setWireframe(on);
    if (m_initialized) update();
}

bool ModelViewport::wireframe() const { return m_renderer.wireframe(); }

void ModelViewport::setShowGrid(bool on)
{
    m_renderer.setShowGrid(on);
    if (m_initialized) update();
}

bool ModelViewport::showGrid() const { return m_renderer.showGrid(); }

void ModelViewport::loadReferenceFile(const QString &mdlPath)
{
    if (!m_initialized) return;

    QString ascii = readMdlToAscii(mdlPath);
    if (ascii.isEmpty()) return;

    MdlScene scene;
    if (!scene.loadFromString(ascii)) return;

    makeCurrent();
    m_renderer.prepareReferenceModel(this, scene, QFileInfo(mdlPath).absolutePath());
    doneCurrent();
    update();
}

void ModelViewport::clearReference()
{
    if (!m_initialized) return;
    makeCurrent();
    m_renderer.clearReferenceModel(this);
    doneCurrent();
    update();
}

bool ModelViewport::hasReference() const { return m_renderer.showReference(); }

void ModelViewport::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePos = event->position();
    m_pressedButtons = event->buttons();
    event->accept();
}

void ModelViewport::mouseMoveEvent(QMouseEvent *event)
{
    QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();

    if (m_pressedButtons & Qt::MiddleButton) {
        if (event->modifiers() & Qt::ShiftModifier)
            m_camera.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
        else
            m_camera.rotate(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
        update();
    } else if (m_pressedButtons & Qt::RightButton) {
        m_camera.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
        update();
    } else if (m_pressedButtons & Qt::LeftButton) {
        m_camera.rotate(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
        update();
    }

    event->accept();
}

void ModelViewport::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressedButtons = event->buttons();
    event->accept();
}

void ModelViewport::wheelEvent(QWheelEvent *event)
{
    float delta = static_cast<float>(event->angleDelta().y()) / 120.0f;
    m_camera.zoom(delta);
    update();
    event->accept();
}
