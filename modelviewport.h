#ifndef MODELVIEWPORT_H
#define MODELVIEWPORT_H

#include "camera.h"
#include "glcontextguard.h"
#include "mdlanimationplayer.h"
#include "mdlscene.h"
#include "renderer.h"
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QProcess>
#include <QString>
#include <functional>

class QLabel;
class QResizeEvent;

class ModelViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ModelViewport(QWidget *parent = nullptr);
    ~ModelViewport() override;

    void setCliBinaryPath(const QString &path);
    void previewFile(const QString &mdlPath);
    void loadModel(const QString &asciiMdl, const QString &textureDir = QString());
    void clearModel();

    // When on, previewFile of the file already shown (a model remade in
    // place) keeps the camera (zoom, pan, angles) instead of framing it
    // again. Another file is always framed. Off by default.
    void setKeepViewOnReload(bool on) { m_keepViewOnReload = on; }

    // Replaces the text shown on the empty canvas.
    void setEmptyHint(const QString &text);

    // Sets the orbit angles (Camera::setAngles) and repaints, without
    // emitting viewRotated.
    void setViewAngles(float yaw, float pitch);

    void setWireframe(bool on);
    bool wireframe() const;
    void setShowGrid(bool on);
    bool showGrid() const;

    void loadReferenceFile(const QString &mdlPath);
    void clearReference();
    bool hasReference() const;
    // Draw the reference filled and depth-tested with the model instead of
    // as a wireframe behind it (Renderer::setSolidReference).
    void setSolidReference(bool on);
    // Light from the camera instead of fixed in the scene (Renderer::setHeadlight).
    void setHeadlight(bool on);

    Camera &camera() { return m_camera; }
    const Camera &camera() const { return m_camera; }

    // Animation control. Lists the animation names parsed from the loaded
    // MDL (empty until a model is loaded). Pass an empty string to stop
    // playback and snap back to bind pose.
    QStringList animationNames() const;
    QString currentAnimation() const;
    void playAnimation(const QString &name);
    bool isPlayingAnimation() const;

    static bool fileIsBinaryMdl(const QString &path);

signals:
    void previewError(const QString &msg);
    // Non-fatal load warnings (parser hit a cap, dropped nodes, etc.).
    // Distinct from previewError because the model DID load and is
    // visible — the warning just tells the user the scene is partial.
    // The main window relays these to the debug log so end users can
    // tell a "looks wrong" model apart from a "looks right" one.
    void previewWarning(const QString &msg);
    // Fires after a model loads with the freshly-parsed animation list.
    // Receivers (e.g. the toolbar combobox) should rebuild their UI.
    void animationsAvailable(const QStringList &names, const QString &nowPlaying);
    // Fires when the user orbits the view with the mouse.
    void viewRotated(float yaw, float pitch);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    // Pause the 60Hz animation tick when the widget is invisible (tab
    // switched away, dock hidden, window minimised) and resume on show.
    // Without this, hidden viewports keep CPU-skinning + uploading +
    // scheduling repaints they'll never actually paint.
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Reads `mdlPath` and asynchronously delivers the ASCII representation to
    // `onSuccess`, or a human-readable failure message to `onError`. ASCII
    // sources resolve synchronously inside this call; binary sources spawn a
    // `cleanmodels decompile` subprocess and resolve when it finishes. A
    // watchdog timer kills runaway processes after CliDefaults::ProcessTimeoutMs.
    void decompileAsync(const QString &mdlPath,
                        std::function<void(const QString &)> onSuccess,
                        std::function<void(const QString &)> onError);

    // Wrap (GlContextGuard → updateAnimatedMeshes → update()) in a
    // single helper. Multiple call sites (timer tick, loadModel post-
    // play, playAnimation) need to push exactly the same animated-
    // frame sequence: re-skin every mesh against the player's current
    // bone-world matrices and request a repaint. GlContextGuard
    // handles the makeCurrent/doneCurrent pairing; this function
    // exists to keep the rest of the sequence in one place so the
    // updateAnimatedMeshes/update() pair can't drift apart.
    void pushAnimatedFrame();

    // Restart the 60Hz animation tick if (and only if) the widget is
    // visible AND a model is loaded AND the player is in Playing state.
    // No-op otherwise. Single source of truth for the (m_clock.start();
    // m_animTimer.start()) pair — without it the precondition check was
    // open-coded at three call sites (loadModel, playAnimation,
    // showEvent) with subtly different conditions.
    void startAnimationTickIfVisible();

    // Symmetric counterpart to startAnimationTickIfVisible: stop the
    // 60Hz tick and invalidate the dt clock so the next start gets a
    // fresh baseline. Open-coded at four sites before extraction
    // (hideEvent, loadModel, playAnimation, clearModel).
    void stopAnimationTick();

    // Shows or hides the "double-click a file" hint. It is visible exactly
    // while no model is loaded, so an empty canvas explains how to fill it.
    void updateEmptyHint();

    QLabel *m_emptyHint = nullptr;

    // previewFile / loadReferenceFile calls made before initializeGL (a
    // viewport not shown yet) are kept here and run once GL is up.
    QString m_pendingPreview;
    QString m_pendingReference;

    Renderer m_renderer;
    Camera m_camera;
    QString m_cliBinaryPath;
    bool m_initialized = false;
    bool m_hasModel = false;
    bool m_keepViewOnReload = false;
    QString m_modelPath;       // the file previewFile last loaded
    bool m_reloadSame = false; // the load in progress is of that same file

    // Live model + animation state. m_scene owns the parsed MDL data so the
    // renderer can re-skin per frame; m_player drives bone matrices over
    // time; m_animTimer advances the player at ~60 Hz; m_clock measures
    // delta-time between ticks for smooth playback regardless of timer
    // jitter.
    MdlScene m_scene;
    MdlAnimationPlayer m_player;
    QTimer m_animTimer;
    QElapsedTimer m_clock;

    QPointF m_lastMousePos;
    Qt::MouseButtons m_pressedButtons;
};

#endif
