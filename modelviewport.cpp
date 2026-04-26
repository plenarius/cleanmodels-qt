#include "constants.h"
#include "modelviewport.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>

ModelViewport::ModelViewport(QWidget *parent)
    : QOpenGLWidget(parent)
{
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

bool ModelViewport::fileIsBinaryMdl(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QByteArray hdr = f.read(4);
    f.close();
    return hdr.size() == 4 && hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 0 && hdr[3] == 0;
}

void ModelViewport::decompileAsync(const QString &mdlPath,
                                   std::function<void(const QString &)> onSuccess,
                                   std::function<void(const QString &)> onError)
{
    if (!fileIsBinaryMdl(mdlPath)) {
        QFile f(mdlPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            onError("Cannot open " + mdlPath);
            return;
        }
        onSuccess(QString::fromUtf8(f.readAll()));
        return;
    }

    if (m_cliBinaryPath.isEmpty()) {
        onError("CLI binary path not set");
        return;
    }

    auto *proc = new QProcess(this);
    proc->setProgram(m_cliBinaryPath);
    proc->setArguments({CliCommand::Decompile, mdlPath});

    // Watchdog: bound the runtime so a stuck decompile cannot leak forever.
    // `proc` parents the timer, so destroying the process tears down the timer.
    auto *watchdog = new QTimer(proc);
    watchdog->setSingleShot(true);
    QObject::connect(watchdog, &QTimer::timeout, proc, [proc, mdlPath, onError]() {
        if (proc->state() == QProcess::NotRunning)
            return;
        proc->kill();
        onError("CLI timed out decompiling " + mdlPath);
    });

    QObject::connect(proc, &QProcess::errorOccurred, this,
                     [proc, mdlPath, onError](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            onError("CLI failed to start for " + mdlPath + ": " + proc->errorString());
            proc->deleteLater();
        }
    });

    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                     [proc, watchdog, mdlPath, onSuccess, onError](int exitCode, QProcess::ExitStatus status) {
        watchdog->stop();
        if (status == QProcess::CrashExit) {
            onError("CLI crashed while decompiling " + mdlPath);
        } else if (exitCode != 0) {
            const QByteArray err = proc->readAllStandardError();
            onError("CLI error decompiling " + mdlPath + ": " +
                    (err.isEmpty() ? QStringLiteral("exit code %1").arg(exitCode)
                                   : QString::fromUtf8(err)));
        } else {
            onSuccess(QString::fromUtf8(proc->readAllStandardOutput()));
        }
        proc->deleteLater();
    });

    proc->start(QIODevice::ReadOnly);
    watchdog->start(CliDefaults::ProcessTimeoutMs);
}

void ModelViewport::previewFile(const QString &mdlPath)
{
    if (!m_initialized) {
        emit previewError("Viewport not initialized");
        return;
    }

    QPointer<ModelViewport> self(this);
    decompileAsync(mdlPath,
        [self, mdlPath](const QString &ascii) {
            if (!self) return;
            if (ascii.isEmpty()) {
                emit self->previewError("Empty MDL content for " + mdlPath);
                return;
            }
            self->loadModel(ascii, QFileInfo(mdlPath).absolutePath());
        },
        [self](const QString &err) {
            if (!self) return;
            emit self->previewError(err);
        });
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

    QPointer<ModelViewport> self(this);
    decompileAsync(mdlPath,
        [self, mdlPath](const QString &ascii) {
            if (!self || ascii.isEmpty()) return;
            MdlScene scene;
            if (!scene.loadFromString(ascii)) return;
            self->makeCurrent();
            self->m_renderer.prepareReferenceModel(self, scene, QFileInfo(mdlPath).absolutePath());
            self->doneCurrent();
            self->update();
        },
        [](const QString &) {
            // Reference loads are best-effort; failures stay silent here so
            // they don't interrupt a decompile preview already on screen.
        });
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
