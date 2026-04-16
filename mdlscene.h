#ifndef MDLSCENE_H
#define MDLSCENE_H

#include <QMatrix4x4>
#include <QVector3D>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <cstdint>

struct MdlFace {
    int32_t verts[3];
    int32_t uvs[3];
    int32_t smoothGroup;
    int32_t material;
};

struct MdlNode {
    QString name;
    QString parent;
    QString nodeType;

    QVector3D position;
    QQuaternion orientation;
    float scale = 1.0f;

    QVector3D ambient{0.2f, 0.2f, 0.2f};
    QVector3D diffuse{0.8f, 0.8f, 0.8f};
    QVector3D specular{0.0f, 0.0f, 0.0f};
    float shininess = 1.0f;
    QString bitmap;
    int render = 1;

    QVector<QVector3D> verts;
    QVector<QVector3D> normals;
    QVector<QVector3D> tverts;
    QVector<MdlFace> faces;

    bool hasMesh() const;
};

class MdlScene
{
public:
    bool loadFromString(const QString &ascii);

    const QVector<MdlNode> &nodes() const { return m_nodes; }
    int rootIndex() const;
    QVector<int> childrenOf(int idx) const;

    void computeBounds(QVector3D &bmin, QVector3D &bmax) const;

private:
    QVector<MdlNode> m_nodes;

    void parseNodeBlock(const QStringList &lines, int &pos);
    void computeBoundsRecursive(int idx, const QMatrix4x4 &parentWorld,
                                QVector3D &bmin, QVector3D &bmax, bool &any) const;
};

#endif
