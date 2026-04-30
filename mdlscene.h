#ifndef MDLSCENE_H
#define MDLSCENE_H

#include <QHash>
#include <QMatrix4x4>
#include <QVector3D>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

struct MdlFace {
    int32_t verts[3];
    int32_t uvs[3];
    int32_t smoothGroup;
    int32_t material;
};

struct MdlVertWeight {
    QString boneName;
    float weight;
};

// Full keyframe lists for one node within one animation. Times are seconds
// from animation start, sorted ascending. Empty channels mean "node not
// animated by this controller — fall back to bind pose".
struct MdlAnimNodeChannels {
    QVector<float> posTimes;
    QVector<QVector3D> posValues;
    QVector<float> oriTimes;
    QVector<QQuaternion> oriValues;
    QVector<float> sclTimes;
    QVector<float> sclValues;

    bool isEmpty() const {
        return posTimes.isEmpty() && oriTimes.isEmpty() && sclTimes.isEmpty();
    }
};

struct MdlAnimation {
    QString name;
    float length = 0.0f;     // seconds; from "length T" in the anim header
    float transTime = 0.0f;  // seconds; transition fade hint, unused for now
    QString animRoot;        // subtree to apply to ("animroot NAME")
    QHash<QString, MdlAnimNodeChannels> channels; // key: node name
};

struct MdlNode {
    QString name;
    QString parent;
    QString nodeType;

    QVector3D position;
    QQuaternion orientation;
    float scale = 1.0f;

    // Bind-pose copies preserved through any later applyPose* override.
    // Skinning needs both the bind-pose bone transforms (to invert) and
    // the current bone transforms (to drive deformation).
    QVector3D bindPosition;
    QQuaternion bindOrientation;

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

    // Skin-node only: per-vertex bone-weight pairs.
    // weights[i] is the weight list for verts[i].
    QVector<QVector<MdlVertWeight>> weights;

    bool hasMesh() const;
};

class MdlScene
{
public:
    bool loadFromString(const QString &ascii);

    const QVector<MdlNode> &nodes() const { return m_nodes; }
    int rootIndex() const;
    QVector<int> childrenOf(int idx) const;
    int findNodeByName(const QString &name) const;

    // World-space transform of node `idx`, computed by walking the parent
    // chain at the *current* pose (bind pose unless an animation has been
    // applied via applyPose*). Returns identity for invalid idx.
    QMatrix4x4 worldTransformOf(int idx) const;

    // World-space transform of node `idx` at the bind pose, regardless of
    // whether applyPose* has been called. Required for skinning so we can
    // invert the bind transforms.
    QMatrix4x4 bindWorldTransformOf(int idx) const;

    // For a skin node, returns its verts in model-space coordinates after
    // linear-blend skinning by each vertex's weighted bones:
    //   v' = Σ w_i * (bone_i.currentWorld * bone_i.bindWorld^-1) * v_modelBind
    // When no animation has been applied, currentWorld == bindWorld for every
    // bone and the result equals the bind-pose model-space verts. Non-skin
    // nodes (or invalid idx) get their raw verts returned unchanged.
    QVector<QVector3D> skinnedVerts(int idx) const;

    void computeBounds(QVector3D &bmin, QVector3D &bmax) const;

    // Names of all parsed animations, in file order.
    QStringList animationNames() const;

    // Read-only access to parsed animations for the player.
    const QVector<MdlAnimation> &animations() const { return m_animations; }
    const MdlAnimation *findAnimation(const QString &name) const;

    // Override each node's bind-pose position/orientation with the first
    // keyframe of `animName`. Returns true if the animation existed and
    // was applied. Use this for static "stand" pose preview when the
    // bare bind pose is too awkward to be recognizable.
    bool applyPoseFrame0(const QString &animName);

    // Apply the first animation found in `preferred`. Returns the name
    // applied, or empty string if none of them existed.
    QString applyPreferredPose(const QStringList &preferred);

    // Skin variant that uses caller-supplied bone world matrices instead
    // of the bind pose. Maps bone-name -> world-space transform. Bones not
    // in the map fall back to bindWorldTransformOf (treating them as if
    // they're at rest). Equivalent to skinnedVerts(idx) when the map is
    // empty.
    QVector<QVector3D> skinnedVertsWith(int idx,
        const QHash<QString, QMatrix4x4> &boneWorld) const;

private:
    QVector<MdlNode> m_nodes;
    QVector<MdlAnimation> m_animations;

    void parseNodeBlock(const QStringList &lines, int &pos);
    void parseAnimBlock(const QStringList &lines, int &pos);
    void parseAnimNodeBlock(const QStringList &lines, int &pos, MdlAnimation &anim);
    void computeBoundsRecursive(int idx, const QMatrix4x4 &parentWorld,
                                QVector3D &bmin, QVector3D &bmax, bool &any) const;
};

#endif
