#ifndef MDLSCENE_H
#define MDLSCENE_H

#include <QHash>
#include <QMatrix4x4>
#include <QSet>
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

struct MdlNode;

// Build a local transform matrix from a (translate, rotate, scale) triple
// using NWN's convention: translate first, then rotate, then uniform scale.
// Lives in the header so the parser, animation player, and renderer all
// build identical local transforms without copy-pasting the four lines.
inline QMatrix4x4 makeLocalTransform(const QVector3D &pos,
                                     const QQuaternion &ori,
                                     float scl)
{
    QMatrix4x4 m;
    m.translate(pos);
    m.rotate(ori);
    if (scl != 1.0f)
        m.scale(scl);
    return m;
}

// Convenience overload for the common case "build the bind-pose local
// transform of this node". Defined out-of-line below so it can see
// MdlNode's full definition without a circular declaration.
inline QMatrix4x4 makeLocalTransform(const MdlNode &node);

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

    // Bind-pose local transform from the parsed scene. The scene is never
    // mutated after parsing — animated transforms live in MdlAnimationPlayer
    // and are composed at render time, so position/orientation/scale here
    // always describe the rest pose.
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

    // Skin-node only: per-vertex bone-weight pairs.
    // weights[i] is the weight list for verts[i].
    QVector<QVector<MdlVertWeight>> weights;

    bool hasMesh() const;
    bool isSkin() const { return nodeType == "skin" && !weights.isEmpty(); }
};

inline QMatrix4x4 makeLocalTransform(const MdlNode &node)
{
    return makeLocalTransform(node.position, node.orientation, node.scale);
}

// Holds a parsed MDL scene plus a few precomputed indices
// (m_nodeIndexByName, m_childrenByParent) that downstream code reads
// hot. Lifetime invariant: m_nodes is populated only by
// loadFromString and never mutated thereafter; the caches are valid
// from the moment loadFromString returns true until the next
// loadFromString call (which clears and rebuilds them atomically).
// Do not add APIs that mutate m_nodes (or any field referenced by a
// cache, e.g. MdlNode::name / MdlNode::parent) without rebuilding
// every cache or the graph walks will silently see a stale view.
class MdlScene
{
public:
    bool loadFromString(const QString &ascii);

    // Non-fatal warnings produced during the most recent successful
    // loadFromString. Examples: "MDL exceeds 4096-node cap; 3217
    // nodes dropped." UI layers should surface these so end users can
    // tell that a model loaded as a partial scene rather than
    // "looking wrong" with no explanation. Cleared at the start of
    // every loadFromString.
    const QStringList &loadWarnings() const { return m_loadWarnings; }

    const QVector<MdlNode> &nodes() const { return m_nodes; }
    int rootIndex() const;
    // Returns a const reference into the precomputed parent->children
    // table built once in loadFromString. Callers that walk the scene
    // graph (renderer, bounds, animation player) used to call this
    // O(N) per visited node, paying O(N²) total for what is morally
    // a single tree traversal. The cache makes each call O(1) plus
    // the cost of any QVector<int> copy the caller forces; prefer
    // `const auto &c = childrenOf(idx)` to avoid the copy.
    const QVector<int> &childrenOf(int idx) const;

    // O(1) name -> index lookup backed by m_nodeIndexByName. Returns the
    // first matching index for duplicate names, mirroring legacy ordering.
    int findNodeByName(const QString &name) const;

    // World-space transform of node `idx` at the parsed (bind) pose.
    // Iterative + visited-set guarded: a malformed MDL with a parent cycle
    // returns identity for the cycle members instead of overflowing the
    // stack. Required by skinning math (we invert it to lift verts into
    // bone-local space).
    QMatrix4x4 bindWorldTransformOf(int idx) const;

    // For a skin node, returns its verts in model-space coordinates after
    // linear-blend skinning by each vertex's weighted bones:
    //   v' = Σ w_i * (bone_i.currentWorld * bone_i.bindWorld^-1) * v_modelBind
    // `boneWorld` supplies the *current* world transforms (from
    // MdlAnimationPlayer), keyed by node index. Bones missing from the
    // map fall back to their bind world, which collapses to identity
    // deform. An empty `boneWorld` therefore returns bind-pose model-
    // space verts. Non-skin nodes (or invalid idx) get their raw verts
    // back unchanged.
    QVector<QVector3D> skinnedVertsWith(int idx,
        const QHash<int, QMatrix4x4> &boneWorld) const;

    void computeBounds(QVector3D &bmin, QVector3D &bmax) const;

    // Names of all parsed animations, in file order.
    QStringList animationNames() const;

    // Read-only access to parsed animations for the player.
    const QVector<MdlAnimation> &animations() const { return m_animations; }
    const MdlAnimation *findAnimation(const QString &name) const;

private:
    QVector<MdlNode> m_nodes;
    // Name -> first-occurrence index. Built once in loadFromString so
    // findNodeByName is O(1) even when the animation player walks parent
    // chains for every node every frame.
    QHash<QString, int> m_nodeIndexByName;
    // parent-index -> list of child indices. Built once in
    // loadFromString from the parsed (parent-name, child-index) pairs;
    // mirrors the original linear-scan childrenOf semantics including
    // the "child appears under every parent that shares the name"
    // behavior for malformed MDLs with duplicate node names.
    QVector<QVector<int>> m_childrenByParent;
    QVector<MdlAnimation> m_animations;
    // Warnings accumulated during the most recent loadFromString.
    // Counters during parse (e.g. m_nodesDroppedByCap) feed a
    // single coalesced warning string at the end of load — the
    // alternative of one warning per cap hit would flood the UI
    // when an attacker-crafted MDL claims thousands of nodes.
    QStringList m_loadWarnings;
    int m_nodesDroppedByCap = 0;
    int m_nodesDroppedByDepth = 0;
    // Scene-wide running totals across all node geometry. The per-node
    // / per-array caps in mdlscene.cpp's anonymous namespace bound any
    // *single* declaration, but their product (kMaxNodes * kMaxArraySize
    // ≈ 4·10^10 verts) is unbounded in aggregate. These counters let
    // parseNodeBlock skip any verts/faces declaration that would push
    // the scene past kMaxTotalVerts / kMaxTotalFaces, so a fan of 50
    // nodes each declaring 1M verts can't sneak past the per-node cap
    // and OOM the process. Counters drop hits feed a coalesced warning
    // surfaced via loadWarnings().
    qint64 m_totalVerts = 0;
    qint64 m_totalFaces = 0;
    int m_vertsDroppedByCap = 0;
    int m_facesDroppedByCap = 0;

    // `depth` bounds nested-`node` recursion. NWN's deepest legitimate
    // hierarchy is well under 100 levels; we cap conservatively to keep
    // a malicious MDL with thousands of `node ... node ... node`
    // (no intervening `endnode`) from blowing the stack.
    void parseNodeBlock(const QStringList &lines, int &pos, int depth = 0);
    void parseAnimBlock(const QStringList &lines, int &pos);
    void parseAnimNodeBlock(const QStringList &lines, int &pos, MdlAnimation &anim);
    // Iterative DFS over the parent->child graph from `rootIdx`. The
    // `visited` set short-circuits cyclic chains (two nodes claiming
    // each other as parent, or longer cycles); the explicit
    // worklist also keeps deep linear chains (thousands of nodes
    // parented head-to-tail) bounded by heap size instead of thread
    // stack size.
    void accumulateBoundsFromRoot(int rootIdx, const QMatrix4x4 &rootParentWorld,
                                  QVector3D &bmin, QVector3D &bmax, bool &any,
                                  QSet<int> &visited) const;
};

#endif
