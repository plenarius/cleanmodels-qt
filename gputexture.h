#ifndef GPUTEXTURE_H
#define GPUTEXTURE_H

#include <QOpenGLFunctions_3_3_Core>
#include <QString>

class GpuTexture
{
public:
    GpuTexture() = default;
    ~GpuTexture() = default;

    bool loadFromFile(QOpenGLFunctions_3_3_Core *gl, const QString &path);
    void bind(QOpenGLFunctions_3_3_Core *gl, int unit = 0) const;
    void destroy(QOpenGLFunctions_3_3_Core *gl);

    bool isValid() const { return m_texture != 0; }

private:
    GLuint m_texture = 0;

    bool loadTGA(QOpenGLFunctions_3_3_Core *gl, const QString &path);
    bool loadBioDDS(QOpenGLFunctions_3_3_Core *gl, const QString &path);
    bool uploadRGBA(QOpenGLFunctions_3_3_Core *gl, const unsigned char *data,
                    int w, int h, bool hasAlpha);
};

#endif
