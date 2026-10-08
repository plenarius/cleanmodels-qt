#ifndef GLCONTEXTGUARD_H
#define GLCONTEXTGUARD_H

#include <QOpenGLWidget>

// RAII guard that pairs makeCurrent() with doneCurrent() across an
// OpenGL operation. Handles destructor-time cleanup so the GL context
// is restored even if the called code throws or returns early. Lives
// in its own header so any translation unit that drives a
// QOpenGLWidget can use the same idiom without depending on
// modelviewport.h (which would force a transitive include of
// QOpenGLFunctions_3_3_Core, the renderer, the scene, and the
// animation player just to RAII-wrap one make/done pair).
//
// Non-copyable and non-movable on purpose: a single scope must own
// the make/done pairing. Permitting copies/moves would either double-
// release (two guards both call doneCurrent on destruction) or
// silently transfer ownership across scopes, defeating the point of
// having a guard at all.
class GlContextGuard
{
public:
    explicit GlContextGuard(QOpenGLWidget *w) : m_w(w)
    {
        if (m_w) m_w->makeCurrent();
    }
    ~GlContextGuard()
    {
        if (m_w) m_w->doneCurrent();
    }
    GlContextGuard(const GlContextGuard &) = delete;
    GlContextGuard &operator=(const GlContextGuard &) = delete;
    GlContextGuard(GlContextGuard &&) = delete;
    GlContextGuard &operator=(GlContextGuard &&) = delete;

private:
    QOpenGLWidget *m_w;
};

#endif
