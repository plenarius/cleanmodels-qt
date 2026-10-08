#include "mdlscene.h"
#include <QDebug>
#include <QHash>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStack>
#include <QStringList>
#include <QTextStream>
#include <QtMath>
#include <cfloat>

namespace {

// Cap on parser arrays — applies uniformly to verts/faces/normals/tverts/
// weights and to every per-channel keyframe list. Without this a small
// crafted MDL can OOM the process by claiming millions of keyframes per
// channel; 10M keys per channel is ~4 orders of magnitude above NWN's
// realistic worst case (~50 keys/channel) so this only ever rejects abuse.
constexpr int kMaxArraySize = 10'000'000;

// Cap on number of animations + per-animation channels. Same rationale.
constexpr int kMaxAnimations = 10'000;

// Hard cap on total input size to loadFromString. QString uses 16-bit
// QChar internally, so 32M chars = 64 MB of QString memory ≈ 32 MB of
// ASCII source on disk. Real ASCII MDLs are 1–5 MB; the largest
// legitimate tile model I've measured is ~20 MB. 32 MB ASCII is
// generous for a single MDL and keeps a 2 GB attacker file from
// becoming a 4 GB QString plus a 4 GB+ QStringList just to be told
// it's malformed. Hits feed a loadWarning and loadFromString returns
// false (parsed scene is empty).
constexpr qsizetype kMaxInputChars = 32 * 1024 * 1024;

// Scene-wide aggregate caps on geometry. The per-array cap above
// (kMaxArraySize) bounds any single `verts N` / `faces N`
// declaration, but their product across all nodes is unbounded:
// kMaxNodes * kMaxArraySize verts ≈ 4·10^10 entries ≈ 491 TB. A
// crafted MDL with, say, 50 trimesh nodes each declaring 1M verts
// passes every per-node check and still claims 600 MB+ of vert
// storage alone. These caps bound the running scene total. 16M is
// ~256× the largest legitimate BioWare tile model; budget cost at
// the cap is ~192 MB verts + ~512 MB faces, which is generous but
// still survivable on every developer machine cleanmodels-qt
// targets. Hits feed a coalesced loadWarning so the user can tell
// a partial scene from a clean load.
constexpr qint64 kMaxTotalVerts = 16'000'000;
constexpr qint64 kMaxTotalFaces = 16'000'000;
constexpr int kMaxChannelsPerAnim = 100'000;

// Cap on parser node-nesting depth. NWN's real hierarchies bottom out
// around 20 levels; 256 leaves plenty of headroom while keeping a small
// crafted MDL from blowing the parser's recursion stack.
constexpr int kMaxNodeDepth = 256;

// Cap on total node count per scene. Real BioWare content tops out
// well under 1000 nodes per MDL (creatures ~30-100, tiles ~200-500,
// items <20). 4096 is comfortably above any realistic case while
// bounding the worst-case cost of the m_childrenByParent build at
// load time: a duplicate-name attack is still O(N²) entries inside
// the cap (16M ints, ~64 MB — slow, but not catastrophic), and a
// pathological MDL claiming millions of nodes is rejected outright
// rather than spending the load consuming memory before any render
// can happen.
constexpr int kMaxNodes = 4096;

// Decode the `axis_x axis_y axis_z angle_radians` quad starting at
// `tokens[offset]` into a Qt quaternion. Returns identity on bad input.
// Centralizes the "NWN stores radians, Qt wants degrees" conversion that
// was duplicated across the static and keyframe orientation parsers.
QQuaternion parseAxisAngleQuat(const QStringList &tokens, int offset)
{
    if (tokens.size() < offset + 4)
        return {};
    return QQuaternion::fromAxisAndAngle(
        QVector3D(tokens[offset].toFloat(),
                  tokens[offset + 1].toFloat(),
                  tokens[offset + 2].toFloat()),
        qRadiansToDegrees(tokens[offset + 3].toFloat()));
}

// Append a position keyframe `(t, x, y, z)` parsed from `tokens` starting
// at `offset`. Drops silently on undersized input or when the channel has
// already hit the per-channel cap (parser DoS guard).
void appendPosKey(MdlAnimNodeChannels &ch, float t,
                  const QStringList &tokens, int offset)
{
    if (tokens.size() < offset + 3)
        return;
    if (ch.posTimes.size() >= kMaxArraySize)
        return;
    ch.posTimes.append(t);
    ch.posValues.append(QVector3D(tokens[offset].toFloat(),
                                  tokens[offset + 1].toFloat(),
                                  tokens[offset + 2].toFloat()));
}

void appendOriKey(MdlAnimNodeChannels &ch, float t,
                  const QStringList &tokens, int offset)
{
    if (tokens.size() < offset + 4)
        return;
    if (ch.oriTimes.size() >= kMaxArraySize)
        return;
    ch.oriTimes.append(t);
    ch.oriValues.append(parseAxisAngleQuat(tokens, offset));
}

void appendSclKey(MdlAnimNodeChannels &ch, float t,
                  const QStringList &tokens, int offset)
{
    if (tokens.size() < offset + 1)
        return;
    if (ch.sclTimes.size() >= kMaxArraySize)
        return;
    ch.sclTimes.append(t);
    ch.sclValues.append(tokens[offset].toFloat());
}

// Advance `pos` past the next line whose trimmed form is exactly
// `terminator`. Used by the parser short-circuit paths to fast-forward
// out of a block whose contents we've decided not to keep. Strict
// equality (not startsWith) is intentional: a corrupted line like
// "endnode_garbage" must not be treated as a real `endnode` or the
// parser desyncs and starts consuming the next block as data.
//
// IMPORTANT: this is a flat, first-match skipper. It does NOT track
// nested block structure. Use it only when the block being skipped
// cannot legitimately contain a nested `terminator` line (e.g. the
// `doneanim` skip in parseAnimBlock — anim blocks don't nest). For
// node-block skips use skipNodeBlock instead, which balances
// node/endnode pairs to defeat parser-desync attacks where an
// adversarial MDL injects an early fake `endnode` inside a deeper
// child to bleed remaining lines into the parent context.
//
// Behavior on malformed input: if `terminator` is missing entirely
// from the rest of the file, this consumes the remainder (up to
// `lines.size()`) and returns with `pos == lines.size()`. The outer
// parser loop then exits cleanly (its `while (pos < lines.size())`
// guard handles it). The result on a truncated/malformed MDL is "we
// load whatever was parseable before the corrupt block and silently
// drop everything after it" — preferable to a parser desync that
// would either crash or load garbage as real data.
//
// Always terminates: `pos` is incremented unconditionally each
// iteration so the loop is bounded by `lines.size()` even if
// `terminator` never appears.
void skipToTerminator(const QStringList &lines, int &pos,
                      const QString &terminator)
{
    while (pos < lines.size()) {
        const QString line = lines[pos].trimmed();
        pos++;
        if (line == terminator)
            break;
    }
}

// Advance `pos` past the matching `endnode` for the `node` block
// whose opening line is currently at `lines[pos]`. Tracks nested
// `node`/`endnode` pairs so a fake `endnode` line appearing inside a
// deeper child block does not close the outer block prematurely.
//
// Why this exists: skipToTerminator(..., "endnode") is a flat first-
// match skipper. After a parser cap fires (depth or node count) the
// flat skipper used to bleed adversarial lines into the parent
// context — an attacker could craft an MDL where the would-be-
// skipped block contains a nested `node ...` then a fake `endnode`
// early; the flat skipper stops at the fake `endnode`, the parent's
// parseNodeBlock loop then interprets the remaining nested-block
// lines as parent geometry (`verts`, `faces`, ...) and re-incurs
// the very memory pressure the cap was meant to prevent. The
// balanced skipper closes that desync by counting opens and closes.
//
// Caller invariant: when invoked, `lines[pos]` must start with
// "node ". The function consumes that opening line first (depth
// rises to 1) and then scans until depth returns to 0.
//
// Behavior on missing terminator: same as skipToTerminator —
// consumes to lines.size() and the outer parser exits cleanly on
// its bounds check.
//
// Always terminates: `pos` is incremented unconditionally each
// iteration.
void skipNodeBlock(const QStringList &lines, int &pos)
{
    if (pos >= lines.size())
        return;
    pos++;            // consume the opening `node ...` line
    int depth = 1;
    while (pos < lines.size() && depth > 0) {
        const QString line = lines[pos].trimmed();
        pos++;
        if (line.startsWith("node "))
            ++depth;
        else if (line == "endnode")
            --depth;
    }
}

} // namespace

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
    m_nodeIndexByName.clear();
    m_childrenByParent.clear();
    m_animations.clear();
    m_loadWarnings.clear();
    m_nodesDroppedByCap = 0;
    m_nodesDroppedByDepth = 0;
    m_totalVerts = 0;
    m_totalFaces = 0;
    m_vertsDroppedByCap = 0;
    m_facesDroppedByCap = 0;

    // Refuse oversized input before split('\n'), which would amplify
    // an attacker's bytes by 3-5x (UTF-16 expansion + per-line
    // QString allocations). Caller still gets the empty scene and
    // can show the loadWarning to the user.
    if (ascii.size() > kMaxInputChars) {
        const QString msg = QStringLiteral(
            "MDL too large: %1 MB source, max %2 MB. File rejected "
            "before parse to avoid OOM. (Real ASCII MDLs are 1-20 MB; "
            "anything larger is almost certainly a binary file with "
            "the wrong extension or a malformed/crafted input.)")
            .arg(ascii.size() / (1024 * 1024))
            .arg(kMaxInputChars / (1024 * 1024));
        m_loadWarnings.append(msg);
        qWarning().noquote() << msg;
        return false;
    }

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

    // Build the name -> first-occurrence-index map. Walking backward so
    // QHash::insert leaves the lowest matching index for duplicate names,
    // matching the legacy linear-scan-from-zero behavior of findNodeByName.
    m_nodeIndexByName.reserve(m_nodes.size());
    for (int i = m_nodes.size() - 1; i >= 0; --i)
        m_nodeIndexByName.insert(m_nodes[i].name, i);

    // Build the parent->children index. For each child, group it under
    // every node sharing its parent name (matches the old
    // O(N)-per-call linear-scan childrenOf semantics — important
    // because malformed MDLs with duplicate names would otherwise
    // produce a different scene graph after this refactor). For
    // typical scenes the inner lookup is O(1) via the temporary
    // multi-map and the build cost is negligible (~50 hash ops + ~50
    // appends for a creature MDL).
    //
    // Worst-case load cost: a duplicate-name attack (every node
    // shares the same name and parent name) does O(N²) appends here
    // — N=4096 (kMaxNodes) gives 16M ints / ~64 MB. Slow but bounded;
    // unlike the old per-call linear scan, this cost is now incurred
    // unconditionally at load before any render, so the kMaxNodes cap
    // applied during parse is the load-bearing defense against this
    // amplification.
    {
        QHash<QString, QVector<int>> nodesByName;
        nodesByName.reserve(m_nodes.size());
        for (int i = 0; i < m_nodes.size(); ++i)
            nodesByName[m_nodes[i].name].append(i);

        m_childrenByParent.resize(m_nodes.size());
        for (int childIdx = 0; childIdx < m_nodes.size(); ++childIdx) {
            const QString &parentName = m_nodes[childIdx].parent;
            if (parentName.isEmpty() || parentName.toLower() == "null")
                continue;
            auto it = nodesByName.constFind(parentName);
            if (it == nodesByName.constEnd())
                continue;
            for (int parentIdx : it.value()) {
                if (parentIdx != childIdx)
                    m_childrenByParent[parentIdx].append(childIdx);
            }
        }
    }

    // Surface coalesced parser-cap warnings to anyone who polls
    // loadWarnings() (the viewport relays them as previewWarning
    // signals so the UI can tell a partial scene from a successful
    // one). qWarning() too so headless tests / CLI uses see them
    // even when no UI is attached. Coalescing per category keeps the
    // log readable when an attacker MDL claims thousands of nodes.
    if (m_nodesDroppedByCap > 0) {
        const QString msg = QStringLiteral(
            "MDL exceeds %1-node cap: %2 node block(s) dropped during "
            "load. Scene rendered with first %3 nodes; rest of the "
            "model is missing. (Increase kMaxNodes if real content "
            "is hitting this — currently sized for largest BioWare "
            "tiles plus headroom.)")
            .arg(kMaxNodes).arg(m_nodesDroppedByCap).arg(m_nodes.size());
        m_loadWarnings.append(msg);
        qWarning().noquote() << msg;
    }
    if (m_nodesDroppedByDepth > 0) {
        const QString msg = QStringLiteral(
            "MDL exceeds %1-level node-nesting cap: %2 node block(s) "
            "dropped during load. The most deeply nested geometry "
            "in this model is missing.")
            .arg(kMaxNodeDepth).arg(m_nodesDroppedByDepth);
        m_loadWarnings.append(msg);
        qWarning().noquote() << msg;
    }
    if (m_vertsDroppedByCap > 0) {
        const QString msg = QStringLiteral(
            "MDL exceeds %1-vert scene cap: %2 verts array(s) dropped "
            "during load (current scene total %3). Affected meshes "
            "render as bind-pose skeletons or empty geometry.")
            .arg(kMaxTotalVerts).arg(m_vertsDroppedByCap).arg(m_totalVerts);
        m_loadWarnings.append(msg);
        qWarning().noquote() << msg;
    }
    if (m_facesDroppedByCap > 0) {
        const QString msg = QStringLiteral(
            "MDL exceeds %1-face scene cap: %2 faces array(s) dropped "
            "during load (current scene total %3). Affected meshes "
            "have no triangles and will not render.")
            .arg(kMaxTotalFaces).arg(m_facesDroppedByCap).arg(m_totalFaces);
        m_loadWarnings.append(msg);
        qWarning().noquote() << msg;
    }

    return !m_nodes.isEmpty();
}

static QVector3D parseVec3(const QStringList &tokens, int offset = 0)
{
    if (tokens.size() < offset + 3)
        return {};
    return {tokens[offset].toFloat(), tokens[offset + 1].toFloat(), tokens[offset + 2].toFloat()};
}

void MdlScene::parseNodeBlock(const QStringList &lines, int &pos, int depth)
{
    if (depth >= kMaxNodeDepth) {
        // Skip the rest of this (and only this) `node` block instead
        // of recursing further. Without this an attacker-controlled
        // MDL can chain thousands of `node trimesh n0` lines (no
        // `endnode`) and overflow the parser's recursion stack at
        // load time. Use the balanced skipper so a deeper child
        // block's fake `endnode` can't desync the parent parse.
        ++m_nodesDroppedByDepth;
        skipNodeBlock(lines, pos);
        return;
    }
    if (m_nodes.size() >= kMaxNodes) {
        // Cap on total nodes per scene. Without this, an attacker-
        // controlled MDL claiming millions of `node` blocks at depth
        // 0 can OOM the process during parse and amplify the
        // m_childrenByParent build (O(N²) in the duplicate-name case)
        // into a multi-second load hang. Skip past the rest of this
        // block (balanced skip so nested fake-`endnode` injection
        // can't desync); subsequent `node` blocks (siblings or
        // top-level) will hit the same cap and skip too.
        //
        // Note: this is checked before adding a new node, but a
        // parent whose recursive children all hit the cap still gets
        // appended once at the bottom of the function. So the actual
        // ceiling is kMaxNodes ancestors-of-the-cap-line plus one
        // for the parent that owns the over-budget subtree —
        // effectively kMaxNodes + small constant. Treating kMaxNodes
        // as a soft target rather than a hard ceiling is fine
        // because the worst-case post-parse cost (m_childrenByParent
        // ~64 MB at 4096²) doesn't materially change at 4097.
        ++m_nodesDroppedByCap;
        skipNodeBlock(lines, pos);
        return;
    }

    MdlNode node;
    QStringList header = lines[pos].trimmed().split(QRegularExpression("\\s+"));
    if (header.size() >= 3)
    {
        node.nodeType = header[1].toLower();
        node.name = header[2];
    }
    pos++;

    // `Discard` is used when a verts/faces declaration would push the
    // scene past kMaxTotalVerts / kMaxTotalFaces. We can't simply
    // ignore the declaration: the next `count` non-blank lines are
    // raw float/int tuples authored as data, not keywords. If we leave
    // mode = None they'd flow into the keyword dispatch below and
    // either silently mis-parse or trigger weird side effects (e.g. a
    // float-prefixed line tokenised as a `position` directive on
    // `node`). Discard consumes exactly `remaining` lines without
    // touching the node, keeping the parser cursor aligned with where
    // the file says the array ends.
    enum class ArrayMode { None, Verts, Faces, TVerts, Normals, Weights, Discard };
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
            parseNodeBlock(lines, pos, depth + 1);
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
            case ArrayMode::Discard:
                // Aggregate cap hit upstream — drop the line on the
                // floor, just keep the cursor advancing.
                break;
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
            node.orientation = parseAxisAngleQuat(tokens, 1);
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
                if (m_totalVerts + count > kMaxTotalVerts) {
                    // Per-array cap accepted this on its own, but
                    // adding it would push the scene past the
                    // aggregate budget. Discard the array (keeping the
                    // cursor aligned via Discard mode) and bump a
                    // counter that becomes a coalesced loadWarning.
                    ++m_vertsDroppedByCap;
                    mode = ArrayMode::Discard;
                    remaining = count;
                } else {
                    mode = ArrayMode::Verts;
                    remaining = count;
                    node.verts.reserve(count);
                    m_totalVerts += count;
                }
            }
        }
        else if (key == "faces" && tokens.size() >= 2)
        {
            int count = tokens[1].toInt();
            if (count > 0 && count <= kMaxArraySize)
            {
                if (m_totalFaces + count > kMaxTotalFaces) {
                    ++m_facesDroppedByCap;
                    mode = ArrayMode::Discard;
                    remaining = count;
                } else {
                    mode = ArrayMode::Faces;
                    remaining = count;
                    node.faces.reserve(count);
                    m_totalFaces += count;
                }
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

    m_nodes.append(node);
}

int MdlScene::findNodeByName(const QString &name) const
{
    auto it = m_nodeIndexByName.constFind(name);
    return it != m_nodeIndexByName.constEnd() ? it.value() : -1;
}

QMatrix4x4 MdlScene::bindWorldTransformOf(int idx) const
{
    if (idx < 0 || idx >= m_nodes.size())
        return {};

    // Iterative parent walk with a visited-set cycle guard. A malformed MDL
    // with cyclic node.parent links (or two nodes that share a name and
    // mutually parent into each other via findNodeByName) used to recurse
    // until the stack overflowed; we now break the cycle and return what
    // we've accumulated so far instead of crashing. Same chain-then-
    // compose shape as MdlAnimationPlayer::recomputeBoneMatrices, which
    // is the every-node / animated variant of this single-target walk.
    QVector<QMatrix4x4> chain;
    chain.reserve(16);
    QSet<int> visited;
    int cur = idx;
    while (cur >= 0 && cur < m_nodes.size() && !visited.contains(cur))
    {
        visited.insert(cur);
        const MdlNode &n = m_nodes[cur];
        chain.append(makeLocalTransform(n));
        int parentIdx = findNodeByName(n.parent);
        if (parentIdx == cur)
            break;
        cur = parentIdx;
    }

    // Compose top-down: world = root_local * ... * parent_local * self_local
    // chain[size-1] is the root, chain[0] is `idx` itself.
    QMatrix4x4 world;
    for (int i = chain.size() - 1; i >= 0; --i)
        world = world * chain[i];
    return world;
}

void MdlScene::parseAnimBlock(const QStringList &lines, int &pos)
{
    // Short-circuit when the per-scene animation cap is already hit: skip
    // straight past `doneanim` without building any keyframe arrays. A
    // crafted MDL with millions of `newanim` blocks otherwise burns O(N)
    // parser time per additional animation just to discard it later.
    if (m_animations.size() >= kMaxAnimations) {
        skipToTerminator(lines, pos, "doneanim");
        return;
    }

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

    // Short-circuit when the per-animation channel cap is already hit:
    // walk to `endnode` without parsing keyframe lines. Avoids the same
    // wasted-parse-then-discard pattern as parseAnimBlock above.
    if (anim.channels.size() >= kMaxChannelsPerAnim) {
        skipToTerminator(lines, pos, "endnode");
        return;
    }

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
        // whole animation. Encoded as a single keyframe at t=0.
        if (mode == KeyMode::None) {
            if (k0 == "position")    appendPosKey(ch, 0.0f, tokens, 1);
            else if (k0 == "orientation") appendOriKey(ch, 0.0f, tokens, 1);
            else if (k0 == "scale")  appendSclKey(ch, 0.0f, tokens, 1);
            continue;
        }

        // Inside a *key block: each line is "time <value...>".
        bool ok = false;
        float t = tokens[0].toFloat(&ok);
        if (!ok) continue;

        switch (mode) {
        case KeyMode::Position:    appendPosKey(ch, t, tokens, 1); break;
        case KeyMode::Orientation: appendOriKey(ch, t, tokens, 1); break;
        case KeyMode::Scale:       appendSclKey(ch, t, tokens, 1); break;
        case KeyMode::None: break;
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

QVector<QVector3D> MdlScene::skinnedVertsWith(int idx,
    const QHash<int, QMatrix4x4> &boneWorld) const
{
    if (idx < 0 || idx >= m_nodes.size())
        return {};
    const MdlNode &n = m_nodes[idx];
    if (!n.isSkin())
        return n.verts;

    // Verts in a skin node are authored in the skin node's local space; lift
    // them into model space at bind first, since linear-blend skinning is
    // defined on the rest-pose world position of each vertex.
    const QMatrix4x4 skinBindWorld = bindWorldTransformOf(idx);

    // Resolve each unique bone once. Animated world transform comes from
    // boneWorld when supplied (driven by an MdlAnimationPlayer); otherwise
    // we fall back to the bind world transform, in which case deform =
    // identity and the result is the bind-pose model-space verts.
    // Cache keyed by bone name (the weight list addresses bones by name)
    // but the boneWorld lookup itself uses the node index — names can
    // alias in malformed MDLs, indices cannot.
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
                // boneWorld constFind / fallback-to-bindWorld pattern.
                // Same shape appears in Renderer::buildRenderNodes
                // (fallback: parentWorld * makeLocalTransform) and
                // Renderer::updateAnimatedMeshes (fallback: skip). The
                // three fallback bodies differ enough that extraction
                // would obscure intent.
                auto bw = boneWorld.constFind(bi);
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

const QVector<int> &MdlScene::childrenOf(int idx) const
{
    static const QVector<int> empty;
    if (idx < 0 || idx >= m_childrenByParent.size())
        return empty;
    return m_childrenByParent[idx];
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
    QSet<int> visited;
    accumulateBoundsFromRoot(root, identity, bmin, bmax, any, visited);

    if (!any) {
        bmin = QVector3D(-1, -1, -1);
        bmax = QVector3D(1, 1, 1);
    }
}

void MdlScene::accumulateBoundsFromRoot(int rootIdx, const QMatrix4x4 &rootParentWorld,
                                        QVector3D &bmin, QVector3D &bmax, bool &any,
                                        QSet<int> &visited) const
{
    // Iterative DFS on a heap-allocated worklist. Shares its graph-
    // walk shape with Renderer::buildRenderNodes (same childrenOf
    // walk, same cycle guard, same reverse-child push for declaration
    // order) but the per-node math is different: bounds always use
    // the bind-pose hierarchy (`parentWorld * makeLocalTransform`),
    // whereas the renderer prefers `boneWorld[idx]` when the
    // animation player has it. Bounds are therefore the bind-pose
    // AABB and will not track an animated frame's reach — callers
    // wanting an animation-aware bounding box need to walk the
    // renderer's RenderNode list, not this function. The explicit
    // stack keeps deep linear chains bounded by heap size instead of
    // thread stack size; the visited set short-circuits cycles
    // introduced by malformed MDLs. See the matching comment on
    // Renderer::buildRenderNodes for the heap vs stack trade-off
    // (peak worklist is O(depth + max_sibling_fanout) per node).
    QStack<QPair<int, QMatrix4x4>> stack;
    stack.push({rootIdx, rootParentWorld});

    while (!stack.isEmpty()) {
        const auto [idx, parentWorld] = stack.pop();
        if (visited.contains(idx))
            continue;
        visited.insert(idx);

        const MdlNode &node = m_nodes[idx];
        QMatrix4x4 world = parentWorld * makeLocalTransform(node);

        for (const auto &v : node.verts) {
            QVector3D wp = world.map(v);
            bmin.setX(std::min(bmin.x(), wp.x()));
            bmin.setY(std::min(bmin.y(), wp.y()));
            bmin.setZ(std::min(bmin.z(), wp.z()));
            bmax.setX(std::max(bmax.x(), wp.x()));
            bmax.setY(std::max(bmax.y(), wp.y()));
            bmax.setZ(std::max(bmax.z(), wp.z()));
            any = true;
        }

        // Reverse-push children so DFS visits in declaration order
        // (no visible effect on bounds; mirrors buildRenderNodes for
        // consistency).
        const QVector<int> &children = childrenOf(idx);
        for (int i = children.size() - 1; i >= 0; --i)
            stack.push({children[i], world});
    }
}
