#include "gputexture.h"
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDebug>

bool GpuTexture::loadFromFile(QOpenGLFunctions_3_3_Core *gl, const QString &path)
{
    if (m_texture) {
        gl->glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }

    QString suffix = QFileInfo(path).suffix().toLower();

    if (suffix == "tga")
        return loadTGA(gl, path);

    // Fallback: let QImage try
    QImage img(path);
    if (img.isNull()) {
        qWarning() << "GpuTexture: cannot load" << path;
        return false;
    }
    img = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
    return uploadRGBA(gl, img.constBits(), img.width(), img.height(), true);
}

bool GpuTexture::loadTGA(QOpenGLFunctions_3_3_Core *gl, const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QByteArray raw = f.readAll();
    f.close();

    if (raw.size() < 18)
        return false;

    const auto *hdr = reinterpret_cast<const unsigned char *>(raw.constData());
    int idLen      = hdr[0];
    int cmapType   = hdr[1];
    int imgType    = hdr[2];
    int width      = hdr[12] | (hdr[13] << 8);
    int height     = hdr[14] | (hdr[15] << 8);
    int bpp        = hdr[16];
    int descriptor = hdr[17];

    Q_UNUSED(cmapType);

    if (imgType != 2 && imgType != 10) {
        qWarning() << "GpuTexture: unsupported TGA type" << imgType;
        return false;
    }

    if (bpp != 24 && bpp != 32) {
        qWarning() << "GpuTexture: unsupported TGA bpp" << bpp;
        return false;
    }

    int channels = bpp / 8;
    int pixelDataOffset = 18 + idLen;

    int64_t pixelCount = static_cast<int64_t>(width) * height;
    constexpr int64_t kMaxTexturePixels = 8192LL * 8192;
    if (pixelCount <= 0 || pixelCount > kMaxTexturePixels) {
        qWarning() << "GpuTexture: TGA dimensions too large or invalid" << width << "x" << height;
        return false;
    }

    int64_t expectedSize = pixelDataOffset + pixelCount * channels;

    bool topOrigin = (descriptor & 0x20) != 0;
    bool hasAlpha  = (bpp == 32);

    QByteArray rgba(static_cast<qsizetype>(pixelCount * 4), '\0');
    auto *dst = reinterpret_cast<unsigned char *>(rgba.data());
    const auto *src = reinterpret_cast<const unsigned char *>(raw.constData()) + pixelDataOffset;

    if (imgType == 2) {
        if (raw.size() < expectedSize)
            return false;

        for (int64_t i = 0; i < pixelCount; ++i) {
            dst[i * 4 + 0] = src[i * channels + 2]; // R (TGA stores BGR)
            dst[i * 4 + 1] = src[i * channels + 1]; // G
            dst[i * 4 + 2] = src[i * channels + 0]; // B
            dst[i * 4 + 3] = hasAlpha ? src[i * channels + 3] : 255;
        }
    } else {
        int pixel = 0;
        int srcOff = pixelDataOffset;

        while (pixel < pixelCount && srcOff < raw.size()) {
            unsigned char packet = static_cast<unsigned char>(raw[srcOff++]);
            int count = (packet & 0x7F) + 1;

            if (packet & 0x80) {
                // RLE packet
                if (srcOff + channels > raw.size()) break;
                unsigned char b = raw[srcOff++];
                unsigned char g = raw[srcOff++];
                unsigned char r = raw[srcOff++];
                unsigned char a = hasAlpha ? raw[srcOff++] : 255;

                for (int j = 0; j < count && pixel < pixelCount; ++j, ++pixel) {
                    dst[pixel * 4 + 0] = r;
                    dst[pixel * 4 + 1] = g;
                    dst[pixel * 4 + 2] = b;
                    dst[pixel * 4 + 3] = a;
                }
            } else {
                // Raw packet
                for (int j = 0; j < count && pixel < pixelCount; ++j, ++pixel) {
                    if (srcOff + channels > raw.size()) break;
                    dst[pixel * 4 + 0] = raw[srcOff + 2]; // R
                    dst[pixel * 4 + 1] = raw[srcOff + 1]; // G
                    dst[pixel * 4 + 2] = raw[srcOff + 0]; // B
                    dst[pixel * 4 + 3] = hasAlpha ? raw[srcOff + 3] : 255;
                    srcOff += channels;
                }
            }
        }
    }

    if (!topOrigin) {
        // Flip vertically (TGA default is bottom-origin)
        int rowBytes = width * 4;
        QByteArray row(rowBytes, '\0');
        for (int y = 0; y < height / 2; ++y) {
            int topOff = y * rowBytes;
            int botOff = (height - 1 - y) * rowBytes;
            memcpy(row.data(), dst + topOff, rowBytes);
            memcpy(dst + topOff, dst + botOff, rowBytes);
            memcpy(dst + botOff, row.data(), rowBytes);
        }
    }

    return uploadRGBA(gl, dst, width, height, hasAlpha);
}

bool GpuTexture::uploadRGBA(QOpenGLFunctions_3_3_Core *gl,
                            const unsigned char *data, int w, int h, bool hasAlpha)
{
    gl->glGenTextures(1, &m_texture);
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, hasAlpha ? GL_RGBA8 : GL_RGB8,
                     w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    gl->glGenerateMipmap(GL_TEXTURE_2D);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void GpuTexture::bind(QOpenGLFunctions_3_3_Core *gl, int unit) const
{
    gl->glActiveTexture(GL_TEXTURE0 + unit);
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
}

void GpuTexture::destroy(QOpenGLFunctions_3_3_Core *gl)
{
    if (m_texture) {
        gl->glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
}
