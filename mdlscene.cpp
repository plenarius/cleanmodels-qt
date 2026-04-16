#include "mdlscene.h"
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
    QStringList lines = ascii.split('\n');
    int pos = 0;
    while (pos < lines.size())
    {
        QString line = lines[pos].trimmed();
        if (line.startsWith("node "))
        {
            parseNodeBlock(lines, pos);
        }
        else if (line.startsWith("endmodelgeom"))
        {
            break;
        }
        else
        {
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
    enum class ArrayMode { None, Verts, Faces, TVerts, Normals };
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
    }

    m_nodes.append(node);
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
