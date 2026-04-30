#include "mdlanimationplayer.h"

#include "mdlscene.h"

#include <QtMath>
#include <algorithm>

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
    if (m_scene)
        recomputeBoneMatrices();
    else
        m_boneWorld.clear();
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

// Linear search for the keyframe index range that brackets `t`.
// Returns (i0, i1, alpha) where alpha is in [0,1]. For t before the first
// keyframe both return the first; for t after the last, both return the
// last.
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
    // Linear scan — animation key counts are small (< 50 typical).
    for (int i = 1; i < times.size(); ++i) {
        if (t <= times[i]) {
            float span = times[i] - times[i - 1];
            float a = span > 0.0f ? (t - times[i - 1]) / span : 0.0f;
            return {i - 1, i, a};
        }
    }
    return {last, last, 0.0f};
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

    // Walk every node, computing its world transform from the bind
    // hierarchy, substituting animated locals where present. Order of
    // traversal doesn't matter because we resolve parents on demand
    // (and memoize as we go).
    const QVector<MdlNode> &nodes = m_scene->nodes();

    std::function<QMatrix4x4(int)> world = [&](int idx) -> QMatrix4x4 {
        if (idx < 0 || idx >= nodes.size())
            return {};
        const MdlNode &n = nodes[idx];
        auto cached = m_boneWorld.constFind(n.name);
        if (cached != m_boneWorld.constEnd())
            return cached.value();

        // Local transform: animated value if present, else bind.
        QVector3D pos = n.bindPosition;
        QQuaternion ori = n.bindOrientation;
        float scl = n.scale;
        auto al = m_animLocal.constFind(n.name);
        if (al != m_animLocal.constEnd()) {
            if (al.value().hasPos) pos = al.value().position;
            if (al.value().hasOri) ori = al.value().orientation;
            if (al.value().hasScl) scl = al.value().scale;
        }

        QMatrix4x4 local;
        local.translate(pos);
        local.rotate(ori);
        if (scl != 1.0f)
            local.scale(scl);

        QMatrix4x4 w = local;
        int parentIdx = m_scene->findNodeByName(n.parent);
        if (parentIdx >= 0 && parentIdx != idx)
            w = world(parentIdx) * local;

        m_boneWorld.insert(n.name, w);
        return w;
    };

    for (int i = 0; i < nodes.size(); ++i)
        world(i);
}
