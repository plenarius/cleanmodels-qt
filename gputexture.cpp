#include "gputexture.h"
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDebug>
#include <cstring>

namespace {

// Maximum allowed dimension for either width or height. Lowered from 8192 to
// 4096 to bound peak per-texture working set at ~64 MiB (4096*4096*4 RGBA).
// NWN content is typically 256-2048 pixels per side; 4096 is comfortably above
// any realistic texture in production HAKs.
constexpr int     kMaxTextureDim    = 4096;
constexpr int64_t kMaxTexturePixels = static_cast<int64_t>(kMaxTextureDim)
                                      * kMaxTextureDim;

// Cap on the raw on-disk size we'll slurp before any header validation, to
// bound DoS via a hostile or accidental giant file. 64 MiB covers any sane
// texture (kMaxTextureDim^2 BC3-compressed = 16 MiB plus header and mipmap
// chain headroom).
constexpr qint64  kMaxTextureFileSize = 64LL * 1024 * 1024;

inline uint16_t readU16LE(const unsigned char *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t readU32LE(const unsigned char *p)
{
    return  static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

// Open + size-check + slurp. Refuses files larger than kMaxTextureFileSize
// before allocating, so a hostile file can't force us to allocate hundreds
// of MB of RAM just to fail validation a moment later.
bool readTextureFile(const QString &path, QByteArray &out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    if (f.size() > kMaxTextureFileSize) {
        qWarning() << "GpuTexture:" << path << "exceeds"
                   << kMaxTextureFileSize << "byte cap, refusing";
        return false;
    }
    out = f.readAll();
    return true;
}

inline void unpack565(uint16_t c, unsigned char rgb[3])
{
    int r = (c >> 11) & 0x1F;
    int g = (c >> 5)  & 0x3F;
    int b =  c        & 0x1F;
    // Bit-replication scaling (matches reference BC1 decoders).
    rgb[0] = static_cast<unsigned char>((r << 3) | (r >> 2));
    rgb[1] = static_cast<unsigned char>((g << 2) | (g >> 4));
    rgb[2] = static_cast<unsigned char>((b << 3) | (b >> 2));
}

// Decode the 4-colour BC1 RGB palette from an 8-byte BC1 colour block header.
// Always uses the 4-colour interpolation regardless of c0/c1 ordering.
//
// The BC1 spec defines a "punchthrough alpha" mode for c0 <= c1 where
// palette[3] is transparent black. Bioware DDS uses BC1 only for
// channelCount == 3 (RGB-only) content -- the alpha channel is irrelevant
// to the upload path, so transparent black would render as visible black
// artifacts on pixels that happened to land on palette index 3 in
// punchthrough mode (BC1 encoders pick that mode for compression-efficiency
// reasons, not just for transparency). 4-colour interpolation everywhere
// is therefore both correct for our use and avoids that artifact.
//
// BC3's colour half is spec'd as always-4-colour regardless of ordering,
// so this helper is correct there too.
void decodeBC1ColourPalette(const unsigned char *src, unsigned char palette[4][3])
{
    unpack565(readU16LE(src),     palette[0]);
    unpack565(readU16LE(src + 2), palette[1]);
    for (int k = 0; k < 3; ++k) {
        palette[2][k] = static_cast<unsigned char>(
            (2 * palette[0][k] + palette[1][k]) / 3);
        palette[3][k] = static_cast<unsigned char>(
            (palette[0][k] + 2 * palette[1][k]) / 3);
    }
}

// Decode one 4x4 BC1 block into RGBA pixels at (px, py) in `dst` of width `w`.
// Caller (loadBioDDS) guarantees `w` and `h` are multiples of 4 and that
// (px+4) <= w and (py+4) <= h, so per-pixel bounds checks are unnecessary.
void decodeBC1Block(const unsigned char *src, unsigned char *dst,
                    int w, int /*h*/, int px, int py)
{
    unsigned char palette[4][3];
    decodeBC1ColourPalette(src, palette);
    uint32_t indices = readU32LE(src + 4);

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            int idx = (indices >> ((j * 4 + i) * 2)) & 0x3;
            unsigned char *p = dst + ((py + j) * w + (px + i)) * 4;
            p[0] = palette[idx][0];
            p[1] = palette[idx][1];
            p[2] = palette[idx][2];
            p[3] = 255;
        }
    }
}

// Decode one 4x4 BC3 block (BC4-style alpha + BC1-style colour) into RGBA
// pixels. Same alignment guarantees as decodeBC1Block.
void decodeBC3Block(const unsigned char *src, unsigned char *dst,
                    int w, int /*h*/, int px, int py)
{
    unsigned char a0 = src[0];
    unsigned char a1 = src[1];

    unsigned char alphas[8];
    alphas[0] = a0;
    alphas[1] = a1;
    if (a0 > a1) {
        for (int k = 1; k <= 6; ++k)
            alphas[1 + k] = static_cast<unsigned char>(
                ((7 - k) * a0 + k * a1) / 7);
    } else {
        for (int k = 1; k <= 4; ++k)
            alphas[1 + k] = static_cast<unsigned char>(
                ((5 - k) * a0 + k * a1) / 5);
        alphas[6] = 0;
        alphas[7] = 255;
    }

    uint64_t aBits = 0;
    for (int k = 0; k < 6; ++k)
        aBits |= static_cast<uint64_t>(src[2 + k]) << (k * 8);

    unsigned char palette[4][3];
    decodeBC1ColourPalette(src + 8, palette);
    uint32_t cIndices = readU32LE(src + 12);

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            int linear = j * 4 + i;
            int aIdx = static_cast<int>((aBits >> (linear * 3)) & 0x7);
            int cIdx = (cIndices >> (linear * 2)) & 0x3;
            unsigned char *p = dst + ((py + j) * w + (px + i)) * 4;
            p[0] = palette[cIdx][0];
            p[1] = palette[cIdx][1];
            p[2] = palette[cIdx][2];
            p[3] = alphas[aIdx];
        }
    }
}

// In-place vertical flip of an RGBA buffer. Used by both loadTGA and
// loadBioDDS to converge on the bottom-up convention that the upload path
// (and the QImage fallback's .mirrored(false, true)) already uses.
void flipRowsRGBA(unsigned char *dst, int width, int height)
{
    int rowBytes = width * 4;
    QByteArray scratch(rowBytes, '\0');
    for (int y = 0; y < height / 2; ++y) {
        unsigned char *top = dst + y * rowBytes;
        unsigned char *bot = dst + (height - 1 - y) * rowBytes;
        std::memcpy(scratch.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, scratch.data(), rowBytes);
    }
}

} // namespace

bool GpuTexture::loadFromFile(QOpenGLFunctions_3_3_Core *gl, const QString &path)
{
    if (m_texture) {
        gl->glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }

    QString suffix = QFileInfo(path).suffix().toLower();

    if (suffix == "tga")
        return loadTGA(gl, path);

    if (suffix == "dds" && loadBioDDS(gl, path))
        return true;

    // Fallback: let QImage try (covers PNG/JPG/BMP, plus Microsoft DDS
    // when the qt6-image-formats DDS plugin is bundled). The vertical
    // flip here is the canonical orientation we converge on -- see
    // flipRowsRGBA / loadTGA / loadBioDDS.
    QImage img(path);
    if (img.isNull()) {
        qWarning() << "GpuTexture: cannot load" << path;
        return false;
    }
    img = img.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
    return uploadRGBA(gl, img.constBits(), img.width(), img.height(), true);
}

bool GpuTexture::loadTGA(QOpenGLFunctions_3_3_Core *gl, const QString &path)
{
    QByteArray raw;
    if (!readTextureFile(path, raw))
        return false;

    if (raw.size() < 18) {
        qWarning() << "GpuTexture: TGA too small for header" << path;
        return false;
    }

    const auto *hdr = reinterpret_cast<const unsigned char *>(raw.constData());
    int idLen      = hdr[0];
    int cmapType   = hdr[1];
    int imgType    = hdr[2];
    int width      = readU16LE(hdr + 12);
    int height     = readU16LE(hdr + 14);
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
    if (pixelCount <= 0 || pixelCount > kMaxTexturePixels) {
        qWarning() << "GpuTexture: TGA dimensions too large or invalid"
                   << width << "x" << height;
        return false;
    }

    int64_t expectedSize = pixelDataOffset + pixelCount * channels;

    bool topOrigin = (descriptor & 0x20) != 0;
    bool hasAlpha  = (bpp == 32);

    QByteArray rgba(static_cast<qsizetype>(pixelCount * 4), '\0');
    auto *dst = reinterpret_cast<unsigned char *>(rgba.data());
    const auto *src = reinterpret_cast<const unsigned char *>(raw.constData()) + pixelDataOffset;

    if (imgType == 2) {
        if (raw.size() < expectedSize) {
            qWarning() << "GpuTexture: TGA pixel data truncated" << path;
            return false;
        }

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

    // Converge on bottom-up memory before upload (matches the QImage fallback's
    // .mirrored(false, true) and loadBioDDS). Default TGAs are bottom-origin
    // already and need no flip; explicitly top-origin TGAs need to be flipped.
    if (topOrigin)
        flipRowsRGBA(dst, width, height);

    return uploadRGBA(gl, dst, width, height, hasAlpha);
}

// Bioware DDS is NWN's proprietary texture format (not Microsoft DirectDraw
// Surface). It has no magic bytes and a 20-byte header: width, height,
// channelCount, pitchOrLinearSize, alphaPremultiplier (all little-endian).
// Pixel data is BCn-compressed: BC1 when channelCount==3, BC3 when ==4.
// The image is encoded top-row-first; we flip on upload to match the
// rest of the renderer's bottom-origin convention.
//
// Spec: https://nwn.wiki/spaces/NWN1/pages/3473496/DDS
bool GpuTexture::loadBioDDS(QOpenGLFunctions_3_3_Core *gl, const QString &path)
{
    QByteArray raw;
    if (!readTextureFile(path, raw))
        return false;

    constexpr int kHeaderSize = 20;
    if (raw.size() < kHeaderSize) {
        qWarning() << "GpuTexture: Bioware DDS too small for header" << path;
        return false;
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(raw.constData());

    // Microsoft DDS starts with "DDS " -- bail silently so the QImage fallback
    // can pick it up if the qt6-image-formats DDS plugin is available.
    if (bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ')
        return false;

    int width        = static_cast<int>(readU32LE(bytes + 0));
    int height       = static_cast<int>(readU32LE(bytes + 4));
    int channelCount = static_cast<int>(readU32LE(bytes + 8));
    int pitch        = static_cast<int>(readU32LE(bytes + 12));
    // bytes + 16: alphaPremultiplier float, currently unused.

    if (width <= 0 || height <= 0 ||
        width > kMaxTextureDim || height > kMaxTextureDim ||
        (width % 4) != 0 || (height % 4) != 0) {
        qWarning() << "GpuTexture: Bioware DDS dimensions invalid"
                   << width << "x" << height << "in" << path;
        return false;
    }
    if (channelCount != 3 && channelCount != 4) {
        qWarning() << "GpuTexture: Bioware DDS unsupported channelCount"
                   << channelCount << "in" << path;
        return false;
    }

    int blockBytes = (channelCount == 3) ? 8 : 16;
    int blocksW = width / 4;
    int blocksH = height / 4;
    int expectedMain = blocksW * blocksH * blockBytes;

    // Per the spec, pitchOrLinearSize == main-mip byte length. We treat a
    // mismatch as a soft warning rather than a hard reject -- the only thing
    // we actually need is enough bytes to decode the main mip.
    if (pitch != expectedMain) {
        qWarning() << "GpuTexture: Bioware DDS pitch" << pitch
                   << "!= expected" << expectedMain << "for"
                   << width << "x" << height << "ch" << channelCount << path;
    }
    if (raw.size() < kHeaderSize + expectedMain) {
        qWarning() << "GpuTexture: Bioware DDS pixel data truncated" << path;
        return false;
    }

    const unsigned char *blocks = bytes + kHeaderSize;

    QByteArray rgba(static_cast<qsizetype>(width) * height * 4, '\0');
    auto *dst = reinterpret_cast<unsigned char *>(rgba.data());

    if (channelCount == 3) {
        for (int by = 0; by < blocksH; ++by) {
            for (int bx = 0; bx < blocksW; ++bx) {
                const unsigned char *src = blocks + (by * blocksW + bx) * 8;
                decodeBC1Block(src, dst, width, height, bx * 4, by * 4);
            }
        }
    } else {
        for (int by = 0; by < blocksH; ++by) {
            for (int bx = 0; bx < blocksW; ++bx) {
                const unsigned char *src = blocks + (by * blocksW + bx) * 16;
                decodeBC3Block(src, dst, width, height, bx * 4, by * 4);
            }
        }
    }

    flipRowsRGBA(dst, width, height);

    return uploadRGBA(gl, dst, width, height, channelCount == 4);
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
