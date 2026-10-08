#include "mdlanimationplayer.h"

#include "mdlscene.h"

#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>

void MdlAnimationPlayer::setScene(const MdlScene *scene)
{
    m_scene = scene;
    stop();
}

QStringList MdlAnimationPlayer::animationNames() const
{
    return m_scene ? m_scene->animationNames() : QStringList{};
}

float MdlAnimationPlayer::currentLength() const
{
    return m_currentAnim ? m_currentAnim->length : 0.0f;
}

bool MdlAnimationPlayer::play(const QString &name)
{
    if (!m_scene)
        return false;
    const MdlAnimation *anim = m_scene->findAnimation(name);
    if (!anim)
        return false;
    m_currentAnim = anim;
    m_currentName = anim->name;
    m_time = 0.0f;
    m_state = State::Playing;
    recomputeAnimatedTransforms();
    recomputeBoneMatrices();
    return true;
}

QString MdlAnimationPlayer::playPreferred(const QStringList &preferred)
{
    for (const QString &n : preferred) {
        if (play(n))
            return m_currentName;
    }
    return {};
}

void MdlAnimationPlayer::stop()
{
    m_state = State::Stopped;
    m_time = 0.0f;
    m_currentAnim = nullptr;
    m_currentName.clear();
    m_animLocal.clear();
    if (!m_scene) {
        m_boneWorld.clear();
        return;
    }
    // Always recompute against the current scene. An earlier version
    // tried to skip this when state was already Stopped, but
    // setScene(newScene) -> stop() would then leave m_boneWorld keyed to
    // the *previous* scene's indices, and a subsequent loadModel of any
    // model without a preferred-idle animation would render with the
    // old model's bone transforms. The walk is O(N) over a small N
    // (typical creature: ~50 nodes) so the optimization was not worth
    // the correctness foot-gun.
    recomputeBoneMatrices();
}

void MdlAnimationPlayer::pause()
{
    if (m_state == State::Playing)
        m_state = State::Paused;
}

void MdlAnimationPlayer::resume()
{
    if (m_state == State::Paused)
        m_state = State::Playing;
}

bool MdlAnimationPlayer::update(float dt)
{
    if (m_state != State::Playing || !m_currentAnim || !m_scene)
        return false;

    const float length = m_currentAnim->length;
    m_time += dt * m_speed;
    if (length > 0.0f) {
        if (m_time >= length) {
            if (m_looping) {
                m_time = std::fmod(m_time, length);
            } else {
                m_time = length;
                m_state = State::Stopped;
            }
        }
    } else {
        // Static animation (length = 0): just sample at t=0.
        m_time = 0.0f;
    }

    recomputeAnimatedTransforms();
    recomputeBoneMatrices();
    return true;
}

namespace {

// Find the keyframe pair `[i0, i1]` that brackets `t` in the sorted `times`
// vector, plus the lerp alpha in [0,1]. For `t` outside the range both
// indices clamp to the same endpoint and alpha is 0. `times` is asserted
// non-empty by the caller.
struct KeyBracket { int i0; int i1; float alpha; };
KeyBracket bracket(const QVector<float> &times, float t)
{
    if (times.isEmpty())
        return {0, 0, 0.0f};
    const int last = static_cast<int>(times.size()) - 1;
    if (t <= times.first())
        return {0, 0, 0.0f};
    if (t >= times.last())
        return {last, last, 0.0f};
    // upper_bound returns the first element strictly greater than t. Since
    // we already handled t <= first and t >= last, the result is in
    // (begin, end) and (it - 1) is the keyframe at-or-before t.
    auto it = std::upper_bound(times.constBegin(), times.constEnd(), t);
    int i1 = static_cast<int>(it - times.constBegin());
    int i0 = i1 - 1;
    float span = times[i1] - times[i0];
    float a = span > 0.0f ? (t - times[i0]) / span : 0.0f;
    return {i0, i1, a};
}

QVector3D lerpVec3(const QVector3D &a, const QVector3D &b, float t)
{
    return a * (1.0f - t) + b * t;
}

float lerpFloat(float a, float b, float t)
{
    return a * (1.0f - t) + b * t;
}

} // namespace

void MdlAnimationPlayer::recomputeAnimatedTransforms()
{
    m_animLocal.clear();
    if (!m_currentAnim)
        return;

    const float t = m_time;
    for (auto it = m_currentAnim->channels.constBegin();
         it != m_currentAnim->channels.constEnd(); ++it)
    {
        const QString &name = it.key();
        const MdlAnimNodeChannels &ch = it.value();
        AnimatedLocal local;

        if (!ch.posValues.isEmpty()) {
            KeyBracket b = bracket(ch.posTimes, t);
            local.position = (b.i0 == b.i1)
                ? ch.posValues[b.i0]
                : lerpVec3(ch.posValues[b.i0], ch.posValues[b.i1], b.alpha);
            local.hasPos = true;
        }
        if (!ch.oriValues.isEmpty()) {
            KeyBracket b = bracket(ch.oriTimes, t);
            local.orientation = (b.i0 == b.i1)
                ? ch.oriValues[b.i0]
                : QQuaternion::slerp(ch.oriValues[b.i0],
                                     ch.oriValues[b.i1], b.alpha);
            local.hasOri = true;
        }
        if (!ch.sclValues.isEmpty()) {
            KeyBracket b = bracket(ch.sclTimes, t);
            local.scale = (b.i0 == b.i1)
                ? ch.sclValues[b.i0]
                : lerpFloat(ch.sclValues[b.i0], ch.sclValues[b.i1], b.alpha);
            local.hasScl = true;
        }

        if (local.hasPos || local.hasOri || local.hasScl)
            m_animLocal.insert(name, local);
    }
}

void MdlAnimationPlayer::recomputeBoneMatrices()
{
    m_boneWorld.clear();
    if (!m_scene)
        return;

    const QVector<MdlNode> &nodes = m_scene->nodes();

    // Per-node animated local transform, computed once. We need this twice
    // (once when the node is the target, once when it appears as someone
    // else's ancestor) so caching it locally avoids re-doing the
    // makeLocalTransform + animLocal lookup for every descendant.
    QVector<QMatrix4x4> locals(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        const MdlNode &n = nodes[i];
        QVector3D pos = n.position;
        QQuaternion ori = n.orientation;
        float scl = n.scale;
        auto al = m_animLocal.constFind(n.name);
        if (al != m_animLocal.constEnd()) {
            if (al.value().hasPos) pos = al.value().position;
            if (al.value().hasOri) ori = al.value().orientation;
            if (al.value().hasScl) scl = al.value().scale;
        }
        locals[i] = makeLocalTransform(pos, ori, scl);
    }

    // Iterative chain-then-compose. The shape resembles
    // MdlScene::bindWorldTransformOf (also a parent-walk with cycle
    // guard) but this version also memoizes via m_boneWorld so a
    // second visit short-circuits — important for deeply nested
    // skeletons where without the cache we'd redo most ancestor
    // composes once per descendant. For each node we walk up to the
    // root (or until we hit a node whose world matrix is already
    // cached), collecting indices in a chain. Then we multiply
    // locals from the closest known ancestor down to the node,
    // caching each intermediate result. A QSet<int> guards against
    // cyclic node.parent links —
    // a malformed MDL otherwise blows the stack at 60 Hz when the
    // renderer pulls boneWorldMatrices() each tick.
    QSet<int> visited;
    QVector<int> chain;
    chain.reserve(16);
    for (int start = 0; start < nodes.size(); ++start) {
        // The inner cache-hit on m_boneWorld at the top of the parent
        // walk handles already-computed nodes for free: the chain
        // exits empty and the compose loop below is a no-op. One
        // predicate, evaluated lazily inside the walk, instead of two
        // (here and there).
        chain.clear();
        visited.clear();
        int cur = start;
        QMatrix4x4 ancestor;
        bool haveAncestor = false;
        while (cur >= 0 && cur < nodes.size()) {
            if (visited.contains(cur))
                break; // cycle: treat as root
            visited.insert(cur);
            auto cached = m_boneWorld.constFind(cur);
            if (cached != m_boneWorld.constEnd()) {
                ancestor = cached.value();
                haveAncestor = true;
                break;
            }
            chain.append(cur);
            int parentIdx = m_scene->findNodeByName(nodes[cur].parent);
            if (parentIdx == cur)
                break; // self-parent: treat as root
            cur = parentIdx;
        }

        // Compose from the (known) ancestor downward.
        QMatrix4x4 acc = haveAncestor ? ancestor : QMatrix4x4{};
        for (int i = chain.size() - 1; i >= 0; --i) {
            const int idx = chain[i];
            acc = acc * locals[idx];
            m_boneWorld.insert(idx, acc);
        }
    }
}
