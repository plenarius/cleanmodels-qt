#include "constants.h"
#include "modelviewport.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHideEvent>
#include <QPointer>
#include <QShowEvent>
#include <QTimer>

namespace {

// NWN creature idle animations, tried in order. The first one that exists
// becomes the auto-play idle. Identical fallback chain used for both the
// main viewport and the reference-overlay viewport — single source of
// truth so adding a new idle name (e.g. cpause2) updates both paths.
const QStringList &preferredIdleAnimations()
{
    static const QStringList kList = {
        "cpause1", "cstand", "pause1", "stand", "cpause", "cwalk", "default"
    };
    return kList;
}

// Animation tick cadence. The fallback dt is what the timer callback
// uses on the very first tick (when m_clock has not yet been started,
// or right after a hide/show transition). Both numbers must match: a
// 16ms interval driven by a fallback of e.g. 0.030 produces visible
// drift on the first frame of every restart.
constexpr int   kAnimTickIntervalMs = 16;
constexpr float kAnimDefaultDtSeconds = kAnimTickIntervalMs / 1000.0f;

} // namespace

ModelViewport::ModelViewport(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // ~60Hz animation tick. Only runs while a model with active animation
    // is loaded AND the widget is currently visible (hideEvent stops it,
    // showEvent restarts it).
    m_animTimer.setInterval(kAnimTickIntervalMs);
    m_animTimer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        if (!m_initialized || !m_hasModel)
            return;
        const float dt = m_clock.isValid()
            ? static_cast<float>(m_clock.restart()) / 1000.0f
            : kAnimDefaultDtSeconds;
        if (m_player.update(dt))
            pushAnimatedFrame();
    });
}

void ModelViewport::pushAnimatedFrame()
{
    if (!m_initialized)
        return;
    GlContextGuard ctx(this);
    m_renderer.updateAnimatedMeshes(this, m_scene, m_player.boneWorldMatrices());
    update();
}

void ModelViewport::startAnimationTickIfVisible()
{
    if (!m_initialized || !m_hasModel || !isPlayingAnimation() || !isVisible())
        return;
    m_clock.start();
    m_animTimer.start();
}

void ModelViewport::stopAnimationTick()
{
    m_animTimer.stop();
    m_clock.invalidate();
}

void ModelViewport::hideEvent(QHideEvent *event)
{
    // Pause animation work while hidden. We don't tear down state — just
    // stop ticking. showEvent restarts the timer if we still have an
    // animation playing.
    stopAnimationTick();
    QOpenGLWidget::hideEvent(event);
}

void ModelViewport::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    startAnimationTickIfVisible();
}

ModelViewport::~ModelViewport()
{
    if (!m_initialized)
        return;
    GlContextGuard ctx(this);
    m_renderer.shutdown(this);
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

    stopAnimationTick();

    MdlScene fresh;
    const bool ok = fresh.loadFromString(asciiMdl);
    // Relay loadFromString's accumulated warnings on both success and
    // failure paths. Some hard-fail conditions (e.g. input-size cap)
    // populate loadWarnings before returning false, and a bare
    // "Failed to parse MDL scene" error message hides the actual
    // reason from the user.
    for (const QString &w : fresh.loadWarnings())
        emit previewWarning(w);
    if (!ok) {
        emit previewError("Failed to parse MDL scene");
        return;
    }
    m_scene = std::move(fresh);

    // Wire the player to the new scene first so its boneWorldMatrices()
    // reflect either bind pose (no animation) or the chosen idle pose at
    // frame 0. The renderer then uploads geometry once using the same
    // bone world matrices the runtime updates use, so there's no bind-
    // pose flash before the timer kicks in and no duplicate code path.
    m_player.setScene(&m_scene);
    QString playing = m_player.playPreferred(preferredIdleAnimations());

    {
        GlContextGuard ctx(this);
        m_renderer.prepareScene(this, m_scene, m_player.boneWorldMatrices(), textureDir);
    }

    QVector3D bmin, bmax;
    m_scene.computeBounds(bmin, bmax);
    m_camera.focusOnBounds(bmin, bmax);

    m_hasModel = true;
    // Must come after m_hasModel = true; the helper bails out if no
    // model is loaded.
    startAnimationTickIfVisible();
    update();

    emit animationsAvailable(m_scene.animationNames(), playing);
}

QStringList ModelViewport::animationNames() const
{
    return m_scene.animationNames();
}

QString ModelViewport::currentAnimation() const
{
    return m_player.currentName();
}

void ModelViewport::playAnimation(const QString &name)
{
    if (!m_initialized || !m_hasModel)
        return;
    if (name.isEmpty()) {
        m_player.stop();
        stopAnimationTick();
        // One last update so the mesh snaps back to bind pose.
        pushAnimatedFrame();
        return;
    }
    if (!m_player.play(name))
        return;
    pushAnimatedFrame();
    startAnimationTickIfVisible();
}

bool ModelViewport::isPlayingAnimation() const
{
    return m_player.state() == MdlAnimationPlayer::State::Playing;
}

void ModelViewport::clearModel()
{
    if (!m_initialized)
        return;
    stopAnimationTick();
    // setScene(nullptr) calls stop() internally; no need to call both.
    m_player.setScene(nullptr);
    m_scene = MdlScene();
    {
        GlContextGuard ctx(this);
        m_renderer.prepareScene(this, m_scene);
    }
    m_hasModel = false;
    update();
    emit animationsAvailable({}, {});
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
            const bool ok = scene.loadFromString(ascii);
            // Same warning relay as loadModel: relay before checking
            // the return value so input-size-cap (and other hard-fail)
            // warnings reach the user even when the reference scene
            // is empty.
            for (const QString &w : scene.loadWarnings())
                emit self->previewWarning(w);
            if (!ok) return;

            // Use a temporary animation player to evaluate frame 0 of the
            // first available idle animation. The renderer uploads the
            // reference geometry once using the resulting bone-world map;
            // both the player and the scene can then be discarded — the
            // reference is static after upload (see prepareReferenceModel,
            // which clears sceneNodeIdx so no later code dereferences the
            // out-of-scope MdlScene).
            MdlAnimationPlayer tempPlayer;
            tempPlayer.setScene(&scene);
            tempPlayer.playPreferred(preferredIdleAnimations());

            {
                GlContextGuard ctx(self);
                self->m_renderer.prepareReferenceModel(
                    self, scene, tempPlayer.boneWorldMatrices(),
                    QFileInfo(mdlPath).absolutePath());
            }
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
    {
        GlContextGuard ctx(this);
        m_renderer.clearReferenceModel(this);
    }
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
