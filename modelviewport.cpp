#include "modelviewport.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>

ModelViewport::ModelViewport(QWidget *parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
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

    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    qDebug() << "VIEWPORT: OpenGL version:" << (version ? version : "null");
    qDebug() << "VIEWPORT: OpenGL renderer:" << (renderer ? renderer : "null");

    m_renderer.initialize(this);
    m_initialized = true;

    // resizeGL runs before initializeGL, so the viewport was never set
    glViewport(0, 0, width(), height());
    m_camera.setAspectRatio(static_cast<float>(width()) / static_cast<float>(std::max(height(), 1)));

    qDebug() << "VIEWPORT: Initialized successfully, program:" << m_renderer.program();
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
    if (!m_initialized) {
        qDebug() << "VIEWPORT: paintGL called but not initialized";
        return;
    }
    m_renderer.render(this, m_camera);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        qDebug() << "VIEWPORT: GL error after render:" << Qt::hex << err;

    if (m_hasModel) {
        static int paintCount = 0;
        if (paintCount < 3) {
            qDebug() << "VIEWPORT: paintGL with model, nodes:" << m_renderer.renderNodeCount()
                     << "fbo:" << defaultFramebufferObject()
                     << "size:" << size();
            paintCount++;
        }
    }
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

void ModelViewport::previewFile(const QString &mdlPath)
{
    if (!m_initialized) {
        emit previewError("Viewport not initialized");
        return;
    }

    QString ascii;

    if (fileIsBinary(mdlPath))
    {
        if (m_cliBinaryPath.isEmpty()) {
            emit previewError("CLI binary path not set");
            return;
        }

        QProcess proc;
        proc.setProgram(m_cliBinaryPath);
        proc.setArguments({"--decompile-only", mdlPath});
        proc.start(QIODevice::ReadOnly);

        if (!proc.waitForFinished(10000)) {
            emit previewError("CLI timed out decompiling " + mdlPath);
            return;
        }

        if (proc.exitCode() != 0) {
            emit previewError("CLI error: " + proc.readAllStandardError());
            return;
        }

        ascii = QString::fromUtf8(proc.readAllStandardOutput());
    }
    else
    {
        QFile f(mdlPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit previewError("Cannot open " + mdlPath);
            return;
        }
        ascii = QString::fromUtf8(f.readAll());
        f.close();
    }

    if (ascii.isEmpty()) {
        emit previewError("Empty MDL content for " + mdlPath);
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

    qDebug() << "VIEWPORT: loadModel called, nodes:" << scene.nodes().size()
             << "root:" << scene.rootIndex();

    makeCurrent();
    m_renderer.prepareScene(this, scene, textureDir);
    doneCurrent();

    qDebug() << "VIEWPORT: prepareScene done, render nodes:" << m_renderer.renderNodeCount();

    QVector3D bmin, bmax;
    scene.computeBounds(bmin, bmax);
    m_camera.focusOnBounds(bmin, bmax);

    qDebug() << "VIEWPORT: bounds min:" << bmin << "max:" << bmax
             << "camera pos:" << m_camera.position();

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
