#include "mdlscene.h"
#include <QDebug>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <cfloat>

bool MdlNode::hasMesh() const
{
    return !verts.isEmpty() && !faces.isEmpty() &&
           (nodeType == "trimesh" || nodeType == "skin" ||
            nodeType == "danglymesh" || nodeType == "animmesh" ||
            nodeType == "aabb");
}

bool MdlScene::loadFromString(const QString &ascii)
{
    m_nodes.clear();
    m_animations.clear();
    QStringList lines = ascii.split('\n');
    int pos = 0;
    bool inGeom = true;
    while (pos < lines.size())
    {
        QString line = lines[pos].trimmed();
        if (inGeom)
        {
            if (line.startsWith("node "))
            {
                parseNodeBlock(lines, pos);
            }
            else if (line.startsWith("endmodelgeom"))
            {
                inGeom = false;
                pos++;
            }
            else
            {
                pos++;
            }
        }
        else
        {
            if (line.startsWith("newanim "))
                parseAnimBlock(lines, pos);
            else if (line.startsWith("donemodel"))
                break;
            else
                pos++;
        }
    }
    return !m_nodes.isEmpty();
}

static QVector3D parseVec3(const QStringList &tokens, int offset = 0)
{
    if (tokens.size() < offset + 3)
        return {};
    return {tokens[offset].toFloat(), tokens[offset + 1].toFloat(), tokens[offset + 2].toFloat()};
}

void MdlScene::parseNodeBlock(const QStringList &lines, int &pos)
{
    MdlNode node;
    QStringList header = lines[pos].trimmed().split(QRegularExpression("\\s+"));
    if (header.size() >= 3)
    {
        node.nodeType = header[1].toLower();
        node.name = header[2];
    }
    pos++;

    constexpr int kMaxArraySize = 10'000'000;
    enum class ArrayMode { None, Verts, Faces, TVerts, Normals, Weights };
    ArrayMode mode = ArrayMode::None;
    int remaining = 0;

    while (pos < lines.size())
    {
        QString line = lines[pos].trimmed();
        pos++;

        if (line.isEmpty() || line.startsWith('#'))
            continue;

        if (line == "endnode")
            break;

        if (line.startsWith("node "))
        {
            pos--;
            parseNodeBlock(lines, pos);
            continue;
        }

        if (mode != ArrayMode::None && remaining > 0)
        {
            QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            switch (mode)
            {
            case ArrayMode::Verts:
                node.verts.append(parseVec3(tokens));
                break;
            case ArrayMode::Normals:
                node.normals.append(parseVec3(tokens));
                break;
            case ArrayMode::TVerts:
                node.tverts.append(parseVec3(tokens));
                break;
            case ArrayMode::Faces: {
                if (tokens.size() >= 8)
                {
                    MdlFace f;
                    f.verts[0] = tokens[0].toInt();
                    f.verts[1] = tokens[1].toInt();
                    f.verts[2] = tokens[2].toInt();
                    f.smoothGroup = tokens[3].toInt();
                    f.uvs[0] = tokens[4].toInt();
                    f.uvs[1] = tokens[5].toInt();
                    f.uvs[2] = tokens[6].toInt();
                    f.material = tokens[7].toInt();
                    node.faces.append(f);
                }
                break;
            }
            case ArrayMode::Weights: {
                // Format: bone_name weight [bone_name weight ...]
                // Up to 4 bone influences per vertex in practice.
                QVector<MdlVertWeight> wlist;
                for (int i = 0; i + 1 < tokens.size(); i += 2) {
                    bool ok = false;
                    float w = tokens[i + 1].toFloat(&ok);
                    if (ok)
                        wlist.append({tokens[i], w});
                }
                node.weights.append(wlist);
                break;
            }
            default:
                break;
            }
            remaining--;
            if (remaining <= 0)
                mode = ArrayMode::None;
            continue;
        }

        QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;

        QString key = tokens[0].toLower();

        if (key == "parent" && tokens.size() >= 2)
        {
            node.parent = tokens[1];
        }
        else if (key == "position" && tokens.size() >= 4)
        {
            node.position = parseVec3(tokens, 1);
        }
        else if (key == "orientation" && tokens.size() >= 5)
        {
            float x = tokens[1].toFloat();
            float y = tokens[2].toFloat();
            float z = tokens[3].toFloat();
            float w = tokens[4].toFloat();
            node.orientation = QQuaternion::fromAxisAndAngle(QVector3D(x, y, z), qRadiansToDegrees(w));
        }
        else if (key == "scale" && tokens.size() >= 2)
        {
            node.scale = tokens[1].toFloat();
        }
        else if (key == "ambient" && tokens.size() >= 4)
        {
            node.ambient = parseVec3(tokens, 1);
        }
        else if (key == "diffuse" && tokens.size() >= 4)
        {
            node.diffuse = parseVec3(tokens, 1);
        }
        else if (key == "specular" && tokens.size() >= 4)
        {
            node.specular = parseVec3(tokens, 1);
        }
        else if (key == "shininess" && tokens.size() >= 2)
        {
            node.shininess = tokens[1].toFloat();
        }
        else if (key == "bitmap" && tokens.size() >= 2)
        {
            node.bitmap = tokens[1];
        }
        else if (key == "render" && tokens.size() >= 2)
        {
            node.render = tokens[1].toInt();
        }
        else if (key == "verts" && tokens.size() >= 2)
        {
            int count = tokens[1].toInt();
            if (count > 0 && count <= kMaxArraySize)
            {
                mode = ArrayMode::Verts;
                remaining = count;
                node.verts.reserve(count);
            }
        }
        else if (key == "faces" && tokens.size() >= 2)
        {
            int count = tokens[1].toInt();
            if (count > 0 && count <= kMaxArraySize)
            {
                mode = ArrayMode::Faces;
                remaining = count;
                node.faces.reserve(count);
            }
        }
        else if (key == "tverts" && tokens.size() >= 2)
        {
            int count = tokens[1].toInt();
            if (count > 0 && count <= kMaxArraySize)
            {
                mode = ArrayMode::TVerts;
                remaining = count;
                node.tverts.reserve(count);
            }
        }
        else if (key == "normals" && tokens.size() >= 2)
        {
            int count = tokens[1].toInt();
            if (count > 0 && count <= kMaxArraySize)
            {
                mode = ArrayMode::Normals;
                remaining = count;
                node.normals.reserve(count);
            }
        }
        else if (key == "weights" && tokens.size() >= 2)
        {
            int count = tokens[1].toInt();
            if (count > 0 && count <= kMaxArraySize)
            {
                mode = ArrayMode::Weights;
                remaining = count;
                node.weights.reserve(count);
            }
        }
    }

    // Snapshot bind pose. applyPose* will mutate position/orientation in
    // place; skinning needs the originals to invert.
    node.bindPosition = node.position;
    node.bindOrientation = node.orientation;

    m_nodes.append(node);
}

int MdlScene::findNodeByName(const QString &name) const
{
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes[i].name == name)
            return i;
    return -1;
}

QMatrix4x4 MdlScene::worldTransformOf(int idx) const
{
    if (idx < 0 || idx >= m_nodes.size())
        return {};
    const MdlNode &n = m_nodes[idx];
    QMatrix4x4 local;
    local.translate(n.position);
    local.rotate(n.orientation);
    if (n.scale != 1.0f)
        local.scale(n.scale);
    int parentIdx = findNodeByName(n.parent);
    if (parentIdx < 0 || parentIdx == idx)
        return local;
    return worldTransformOf(parentIdx) * local;
}

QMatrix4x4 MdlScene::bindWorldTransformOf(int idx) const
{
    if (idx < 0 || idx >= m_nodes.size())
        return {};
    const MdlNode &n = m_nodes[idx];
    QMatrix4x4 local;
    local.translate(n.bindPosition);
    local.rotate(n.bindOrientation);
    if (n.scale != 1.0f)
        local.scale(n.scale);
    int parentIdx = findNodeByName(n.parent);
    if (parentIdx < 0 || parentIdx == idx)
        return local;
    return bindWorldTransformOf(parentIdx) * local;
}

void MdlScene::parseAnimBlock(const QStringList &lines, int &pos)
{
    QStringList header = lines[pos].trimmed().split(QRegularExpression("\\s+"));
    pos++;
    if (header.size() < 2)
        return;

    MdlAnimation anim;
    anim.name = header[1];

    while (pos < lines.size())
    {
        QString line = lines[pos].trimmed();
        if (line.startsWith("doneanim"))
        {
            pos++;
            break;
        }
        if (line.startsWith("node "))
        {
            parseAnimNodeBlock(lines, pos, anim);
            continue;
        }

        QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tokens.size() >= 2) {
            const QString &k = tokens[0];
            if (k == "length") anim.length = tokens[1].toFloat();
            else if (k == "transtime") anim.transTime = tokens[1].toFloat();
            else if (k == "animroot") anim.animRoot = tokens[1];
        }
        pos++;
    }

    m_animations.append(anim);
}

void MdlScene::parseAnimNodeBlock(const QStringList &lines, int &pos, MdlAnimation &anim)
{
    // First line is "node TYPE NAME". Capture name only.
    QStringList header = lines[pos].trimmed().split(QRegularExpression("\\s+"));
    pos++;
    if (header.size() < 3)
        return;

    QString nodeName = header[2];
    MdlAnimNodeChannels ch;

    enum class KeyMode { None, Position, Orientation, Scale };
    KeyMode mode = KeyMode::None;

    while (pos < lines.size())
    {
        QString line = lines[pos].trimmed();
        pos++;

        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line == "endnode")
            break;

        QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;

        // Controller list openers — may have an optional count after them
        // ("positionkey 5"), or just stand alone. Switch mode and continue.
        const QString &k0 = tokens[0];
        if (k0 == "positionkey") { mode = KeyMode::Position;    continue; }
        if (k0 == "orientationkey") { mode = KeyMode::Orientation; continue; }
        if (k0 == "scalekey")    { mode = KeyMode::Scale;       continue; }
        if (k0 == "endlist")     { mode = KeyMode::None;        continue; }

        // Static (single-keyframe) controllers: the value applies for the
        // whole animation. Encode as a single keyframe at t=0.
        if (mode == KeyMode::None) {
            if (k0 == "position" && tokens.size() >= 4) {
                ch.posTimes.append(0.0f);
                ch.posValues.append(QVector3D(tokens[1].toFloat(),
                                              tokens[2].toFloat(),
                                              tokens[3].toFloat()));
            } else if (k0 == "orientation" && tokens.size() >= 5) {
                float ax = tokens[1].toFloat();
                float ay = tokens[2].toFloat();
                float az = tokens[3].toFloat();
                float ang = tokens[4].toFloat();
                ch.oriTimes.append(0.0f);
                ch.oriValues.append(QQuaternion::fromAxisAndAngle(
                    QVector3D(ax, ay, az), qRadiansToDegrees(ang)));
            } else if (k0 == "scale" && tokens.size() >= 2) {
                ch.sclTimes.append(0.0f);
                ch.sclValues.append(tokens[1].toFloat());
            }
            continue;
        }

        // Inside a *key block: each line is "time <value...>".
        bool ok = false;
        float t = tokens[0].toFloat(&ok);
        if (!ok) continue;

        if (mode == KeyMode::Position && tokens.size() >= 4) {
            ch.posTimes.append(t);
            ch.posValues.append(QVector3D(tokens[1].toFloat(),
                                          tokens[2].toFloat(),
                                          tokens[3].toFloat()));
        } else if (mode == KeyMode::Orientation && tokens.size() >= 5) {
            float ax = tokens[1].toFloat();
            float ay = tokens[2].toFloat();
            float az = tokens[3].toFloat();
            float ang = tokens[4].toFloat();
            ch.oriTimes.append(t);
            ch.oriValues.append(QQuaternion::fromAxisAndAngle(
                QVector3D(ax, ay, az), qRadiansToDegrees(ang)));
        } else if (mode == KeyMode::Scale && tokens.size() >= 2) {
            ch.sclTimes.append(t);
            ch.sclValues.append(tokens[1].toFloat());
        }
    }

    if (!ch.isEmpty())
        anim.channels.insert(nodeName, ch);
}

QStringList MdlScene::animationNames() const
{
    QStringList out;
    out.reserve(m_animations.size());
    for (const auto &a : m_animations)
        out.append(a.name);
    return out;
}

const MdlAnimation *MdlScene::findAnimation(const QString &name) const
{
    for (const auto &a : m_animations)
        if (a.name.compare(name, Qt::CaseInsensitive) == 0)
            return &a;
    return nullptr;
}

bool MdlScene::applyPoseFrame0(const QString &animName)
{
    const MdlAnimation *anim = findAnimation(animName);
    if (!anim)
        return false;
    for (auto &node : m_nodes)
    {
        auto it = anim->channels.constFind(node.name);
        if (it == anim->channels.constEnd())
            continue;
        const MdlAnimNodeChannels &ch = it.value();
        if (!ch.posValues.isEmpty())
            node.position = ch.posValues.first();
        if (!ch.oriValues.isEmpty())
            node.orientation = ch.oriValues.first();
    }
    return true;
}

QString MdlScene::applyPreferredPose(const QStringList &preferred)
{
    for (const QString &name : preferred)
    {
        if (applyPoseFrame0(name))
            return name;
    }
    return {};
}

QVector<QVector3D> MdlScene::skinnedVerts(int idx) const
{
    // Legacy entry point: derive bone "current" matrices from the current
    // (possibly applyPose*-mutated) node positions. Used by the reference
    // model loader, which stamps a static idle pose into the scene rather
    // than running an animation player. With no mutation this is identical
    // to skinnedVertsWith({}, ...) so callers can use either form.
    if (idx < 0 || idx >= m_nodes.size())
        return {};
    const MdlNode &n = m_nodes[idx];
    if (n.nodeType != "skin" || n.weights.isEmpty())
        return n.verts;

    QHash<QString, QMatrix4x4> boneWorld;
    for (const auto &w : n.weights) {
        for (const auto &bw : w) {
            if (boneWorld.contains(bw.boneName))
                continue;
            int bi = findNodeByName(bw.boneName);
            if (bi >= 0)
                boneWorld.insert(bw.boneName, worldTransformOf(bi));
        }
    }
    return skinnedVertsWith(idx, boneWorld);
}

QVector<QVector3D> MdlScene::skinnedVertsWith(int idx,
    const QHash<QString, QMatrix4x4> &boneWorld) const
{
    if (idx < 0 || idx >= m_nodes.size())
        return {};
    const MdlNode &n = m_nodes[idx];
    if (n.nodeType != "skin" || n.weights.isEmpty())
        return n.verts;

    // Verts in a skin node are authored in the skin node's local space; lift
    // them into model space at bind first, since linear-blend skinning is
    // defined on the rest-pose world position of each vertex.
    const QMatrix4x4 skinBindWorld = bindWorldTransformOf(idx);

    // Resolve each unique bone once. Animated world transform comes from
    // boneWorld when supplied (driven by an MdlAnimationPlayer); otherwise
    // we fall back to the bind world transform, in which case deform =
    // identity and the result is the bind-pose model-space verts.
    struct BoneXform {
        QMatrix4x4 deform; // currentWorld * bindWorld^-1
        bool valid;
    };
    QHash<QString, BoneXform> bones;
    auto boneOf = [&](const QString &name) -> BoneXform {
        auto it = bones.find(name);
        if (it != bones.end())
            return it.value();
        BoneXform bx{};
        int bi = findNodeByName(name);
        if (bi >= 0) {
            bool invertible = false;
            QMatrix4x4 bindWorld = bindWorldTransformOf(bi);
            QMatrix4x4 bindInv = bindWorld.inverted(&invertible);
            if (invertible) {
                auto bw = boneWorld.constFind(name);
                const QMatrix4x4 &cur = (bw != boneWorld.constEnd())
                                        ? bw.value() : bindWorld;
                bx.deform = cur * bindInv;
                bx.valid = true;
            }
        }
        bones.insert(name, bx);
        return bx;
    };

    QVector<QVector3D> out;
    out.reserve(n.verts.size());
    for (int i = 0; i < n.verts.size(); ++i) {
        const QVector3D vModel = skinBindWorld.map(n.verts[i]);
        if (i >= n.weights.size() || n.weights[i].isEmpty()) {
            out.append(vModel);
            continue;
        }
        QVector3D acc(0, 0, 0);
        float total = 0.0f;
        for (const auto &w : n.weights[i]) {
            BoneXform bx = boneOf(w.boneName);
            if (!bx.valid)
                continue;
            acc += w.weight * bx.deform.map(vModel);
            total += w.weight;
        }
        if (total <= 0.0f) {
            out.append(vModel);
        } else {
            if (std::abs(total - 1.0f) > 0.001f)
                acc /= total;
            out.append(acc);
        }
    }
    return out;
}

int MdlScene::rootIndex() const
{
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        if (m_nodes[i].parent.toLower() == "null" || m_nodes[i].parent.isEmpty())
            return i;
    }
    return m_nodes.isEmpty() ? -1 : 0;
}

QVector<int> MdlScene::childrenOf(int idx) const
{
    QVector<int> out;
    if (idx < 0 || idx >= m_nodes.size())
        return out;
    const QString &name = m_nodes[idx].name;
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        if (i != idx && m_nodes[i].parent == name)
            out.append(i);
    }
    return out;
}

void MdlScene::computeBounds(QVector3D &bmin, QVector3D &bmax) const
{
    bmin = QVector3D(FLT_MAX, FLT_MAX, FLT_MAX);
    bmax = QVector3D(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    int root = rootIndex();
    if (root < 0) {
        bmin = QVector3D(-1, -1, -1);
        bmax = QVector3D(1, 1, 1);
        return;
    }

    QMatrix4x4 identity;
    bool any = false;
    computeBoundsRecursive(root, identity, bmin, bmax, any);

    if (!any) {
        bmin = QVector3D(-1, -1, -1);
        bmax = QVector3D(1, 1, 1);
    }
}

void MdlScene::computeBoundsRecursive(int idx, const QMatrix4x4 &parentWorld,
                                       QVector3D &bmin, QVector3D &bmax, bool &any) const
{
    const MdlNode &node = m_nodes[idx];

    QMatrix4x4 local;
    local.translate(node.position);
    local.rotate(node.orientation);
    if (node.scale != 1.0f)
        local.scale(node.scale);
    QMatrix4x4 world = parentWorld * local;

    for (const auto &v : node.verts)
    {
        QVector3D wp = world.map(v);
        bmin.setX(std::min(bmin.x(), wp.x()));
        bmin.setY(std::min(bmin.y(), wp.y()));
        bmin.setZ(std::min(bmin.z(), wp.z()));
        bmax.setX(std::max(bmax.x(), wp.x()));
        bmax.setY(std::max(bmax.y(), wp.y()));
        bmax.setZ(std::max(bmax.z(), wp.z()));
        any = true;
    }

    for (int c : childrenOf(idx))
        computeBoundsRecursive(c, world, bmin, bmax, any);
}
