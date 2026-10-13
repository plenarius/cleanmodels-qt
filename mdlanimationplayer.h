#ifndef MDLANIMATIONPLAYER_H
#define MDLANIMATIONPLAYER_H

#include <QHash>
#include <QMatrix4x4>
#include <QString>
#include <QStringList>

class MdlScene;
struct MdlAnimation;

// Plays an MdlAnimation against an MdlScene's bind hierarchy and exposes
// per-bone world matrices for skinning, mirroring the design in
// borealis_nwn_mdl's AnimationPlayer (no shared code, but the same API
// shape: setScene/play/update/bone matrices).
//
// The scene itself is never mutated. Instead we maintain a parallel
// "animated transform" map per bone, then walk the bind hierarchy using
// animated transforms where present and bind transforms elsewhere.
class MdlAnimationPlayer
{
public:
    enum class State { Stopped, Playing, Paused };

    void setScene(const MdlScene *scene);

    QStringList animationNames() const;

    bool play(const QString &name);
    // Try names in order; play the first one that exists. Returns the
    // chosen name (or empty if none matched). Useful for "cpause1, cstand,
    // pause1, stand" fallback chains.
    QString playPreferred(const QStringList &preferred);

    void stop();
    void pause();
    void resume();

    void setLooping(bool b) { m_looping = b; }
    bool isLooping() const { return m_looping; }

    void setSpeed(float s) { m_speed = s; }
    float speed() const { return m_speed; }

    State state() const { return m_state; }
    float currentTime() const { return m_time; }
    float currentLength() const;
    QString currentName() const { return m_currentName; }

    // Advance time by dt seconds (handles looping). Returns true if any
    // animated transform changed since the last call (always true while
    // playing; false while stopped/paused).
    bool update(float dt);

    // World-space animated transform per node, keyed by node index in
    // MdlScene::nodes(). Index keys (rather than names) avoid duplicate-
    // name aliasing in malformed MDLs and are O(1) for the renderer to
    // look up. Falls back to identity for nodes the player never visited
    // (no scene set, or a node unreachable from the parent walk).
    const QHash<int, QMatrix4x4> &boneWorldMatrices() const { return m_boneWorld; }

private:
    void recomputeAnimatedTransforms();
    void recomputeBoneMatrices();

    const MdlScene *m_scene = nullptr;
    const MdlAnimation *m_currentAnim = nullptr;
    QString m_currentName;
    State m_state = State::Stopped;
    float m_time = 0.0f;
    float m_speed = 1.0f;
    bool m_looping = true;

    // Per-bone animated local transform (position/orientation/scale) for the
    // current m_time. Bones absent from the map fall back to bind values.
    struct AnimatedLocal {
        QVector3D position;
        QQuaternion orientation;
        float scale = 1.0f;
        bool hasPos = false;
        bool hasOri = false;
        bool hasScl = false;
    };
    QHash<QString, AnimatedLocal> m_animLocal;

    // World-space transform per node, computed by walking the bind
    // hierarchy with animated locals overriding bind values. Keyed by
    // node index (stable, unique even with duplicate node names).
    QHash<int, QMatrix4x4> m_boneWorld;
};

#endif
