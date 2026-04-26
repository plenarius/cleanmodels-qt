#ifndef MODELVIEWPORT_H
#define MODELVIEWPORT_H

#include "camera.h"
#include "renderer.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QProcess>
#include <QString>
#include <functional>

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

    void setWireframe(bool on);
    bool wireframe() const;
    void setShowGrid(bool on);
    bool showGrid() const;

    void loadReferenceFile(const QString &mdlPath);
    void clearReference();
    bool hasReference() const;

    Camera &camera() { return m_camera; }
    const Camera &camera() const { return m_camera; }

    static bool fileIsBinaryMdl(const QString &path);

signals:
    void previewError(const QString &msg);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Reads `mdlPath` and asynchronously delivers the ASCII representation to
    // `onSuccess`, or a human-readable failure message to `onError`. ASCII
    // sources resolve synchronously inside this call; binary sources spawn a
    // `cleanmodels decompile` subprocess and resolve when it finishes. A
    // watchdog timer kills runaway processes after CliDefaults::ProcessTimeoutMs.
    void decompileAsync(const QString &mdlPath,
                        std::function<void(const QString &)> onSuccess,
                        std::function<void(const QString &)> onError);

    Renderer m_renderer;
    Camera m_camera;
    QString m_cliBinaryPath;
    bool m_initialized = false;
    bool m_hasModel = false;

    QPointF m_lastMousePos;
    Qt::MouseButtons m_pressedButtons;
};

#endif
