// The preview's animroot pruning (nwn-mdl-spec MDL-NODE-045): an animation
// plays only the animroot's subtree and the path down to it when the animroot
// names one of its own nodes, case for case, and its whole tree otherwise.

#include "mdlscene.h"

#include <QStringList>
#include <QTest>

namespace {

QVector<MdlAnimTreeNode> armTree()
{
    // rootdummy
    //   arm
    //     hand
    //   leg
    return {
        { "rootdummy", "NULL" },
        { "arm", "rootdummy" },
        { "hand", "arm" },
        { "leg", "rootdummy" },
    };
}

QStringList keptNames(const QVector<MdlAnimTreeNode> &nodes, const QVector<bool> &kept)
{
    QStringList out;
    for (int i = 0; i < nodes.size(); ++i)
        if (kept.value(i))
            out << nodes[i].name;
    return out;
}

// A model with the same tree in its geometry and one animation that moves
// every node, with the given animroot.
QString armModel(const QString &animRoot)
{
    QString s = QStringLiteral(
        "newmodel test\n"
        "setsupermodel test NULL\n"
        "beginmodelgeom test\n"
        "node dummy rootdummy\n  parent NULL\nendnode\n"
        "node dummy arm\n  parent rootdummy\n  position 0 0 1\nendnode\n"
        "node dummy hand\n  parent arm\n  position 0 0 1\nendnode\n"
        "node dummy leg\n  parent rootdummy\n  position 0 1 0\nendnode\n"
        "endmodelgeom test\n"
        "newanim wave test\n"
        "  length 1\n"
        "  transtime 0.25\n");
    if (!animRoot.isNull())
        s += QStringLiteral("  animroot ") + animRoot + QStringLiteral("\n");
    for (const char *node : { "rootdummy", "arm", "hand", "leg" }) {
        s += QStringLiteral("  node dummy ") + QLatin1String(node) + QStringLiteral("\n");
        s += QLatin1String(node) == QLatin1String("rootdummy")
                 ? QStringLiteral("    parent NULL\n")
                 : QLatin1String(node) == QLatin1String("hand") ? QStringLiteral("    parent arm\n")
                                                                : QStringLiteral("    parent rootdummy\n");
        s += QStringLiteral("    positionkey\n      0 0 0 0\n      1 0 0 1\n    endlist\n  endnode\n");
    }
    s += QStringLiteral("doneanim wave test\ndonemodel test\n");
    return s;
}

QStringList channelNames(const MdlScene &scene)
{
    const MdlAnimation *anim = scene.findAnimation("wave");
    if (!anim)
        return { "<no animation>" };
    QStringList names = anim->channels.keys();
    names.sort();
    return names;
}

} // namespace

class TestAnimRoot : public QObject
{
    Q_OBJECT

private slots:
    void matchKeepsPathAndSubtree()
    {
        const auto nodes = armTree();
        QCOMPARE(keptNames(nodes, MdlScene::animRootKeptNodes(nodes, "arm")),
                 QStringList({ "rootdummy", "arm", "hand" }));
    }

    void matchDeepKeepsOnlyThePath()
    {
        // root > a > b > target > tip, with a sibling at every level.
        const QVector<MdlAnimTreeNode> nodes = {
            { "root", "NULL" },
            { "a", "root" },
            { "a_side", "root" },
            { "b", "a" },
            { "b_side", "a" },
            { "target", "b" },
            { "tip", "target" },
            { "target_side", "b" },
        };
        QCOMPARE(keptNames(nodes, MdlScene::animRootKeptNodes(nodes, "target")),
                 QStringList({ "root", "a", "b", "target", "tip" }));
    }

    void matchLeafKeepsTheLeaf()
    {
        const auto nodes = armTree();
        QCOMPARE(keptNames(nodes, MdlScene::animRootKeptNodes(nodes, "leg")),
                 QStringList({ "rootdummy", "leg" }));
    }

    void parentsMatchInAnyCase()
    {
        // The parent lines name their nodes in another case; the tree is the
        // same, and only the animroot itself must match case for case.
        const QVector<MdlAnimTreeNode> nodes = {
            { "rootdummy", "null" },
            { "arm", "ROOTDUMMY" },
            { "hand", "Arm" },
            { "leg", "RootDummy" },
        };
        QCOMPARE(keptNames(nodes, MdlScene::animRootKeptNodes(nodes, "arm")),
                 QStringList({ "rootdummy", "arm", "hand" }));
    }

    void noMatchKeepsAll()
    {
        const auto nodes = armTree();
        QCOMPARE(keptNames(nodes, MdlScene::animRootKeptNodes(nodes, "tail")),
                 QStringList({ "rootdummy", "arm", "hand", "leg" }));
    }

    void caseMismatchKeepsAll()
    {
        const auto nodes = armTree();
        QCOMPARE(keptNames(nodes, MdlScene::animRootKeptNodes(nodes, "ARM")),
                 QStringList({ "rootdummy", "arm", "hand", "leg" }));
        QCOMPARE(keptNames(nodes, MdlScene::animRootKeptNodes(nodes, "Arm")),
                 QStringList({ "rootdummy", "arm", "hand", "leg" }));
    }

    void rootNullOrEmptyKeepsAll()
    {
        const auto nodes = armTree();
        for (const char *root : { "rootdummy", "NULL", "null", "" })
            QCOMPARE(keptNames(nodes, MdlScene::animRootKeptNodes(nodes, root)),
                     QStringList({ "rootdummy", "arm", "hand", "leg" }));
    }

    void cycleDoesNotHang()
    {
        // a and b claim each other; the walk visits each node once.
        const QVector<MdlAnimTreeNode> nodes = {
            { "root", "NULL" },
            { "a", "root" },
            { "b", "a" },
            { "a", "b" },
            { "c", "root" },
        };
        const QVector<bool> kept = MdlScene::animRootKeptNodes(nodes, "b");
        QCOMPARE(kept.size(), nodes.size());
        QVERIFY(kept[0] && kept[1] && kept[2]);
        QVERIFY(!kept[4]);
    }

    // End to end through the parser: the channels the player gets.
    void parserPrunesOnMatch()
    {
        MdlScene scene;
        QVERIFY(scene.loadFromString(armModel("arm")));
        QCOMPARE(channelNames(scene), QStringList({ "arm", "hand", "rootdummy" }));
    }

    void parserKeepsAllOnNoMatch()
    {
        MdlScene scene;
        QVERIFY(scene.loadFromString(armModel("tail")));
        QCOMPARE(channelNames(scene), QStringList({ "arm", "hand", "leg", "rootdummy" }));
    }

    void parserKeepsAllOnCaseMismatch()
    {
        MdlScene scene;
        QVERIFY(scene.loadFromString(armModel("ARM")));
        QCOMPARE(channelNames(scene), QStringList({ "arm", "hand", "leg", "rootdummy" }));
    }

    void parserKeepsAllWithoutAnimroot()
    {
        MdlScene scene;
        QVERIFY(scene.loadFromString(armModel(QString())));
        QCOMPARE(channelNames(scene), QStringList({ "arm", "hand", "leg", "rootdummy" }));
    }

    void parserIgnoresGeometryOnlyNode()
    {
        // The animroot names a node the geometry has but the animation does
        // not: no match, so the whole tree plays.
        QString model = armModel("arm");
        model.replace("  node dummy arm\n", "  node dummy arm2\n");
        model.replace("    parent arm\n", "    parent arm2\n");
        MdlScene scene;
        QVERIFY(scene.loadFromString(model));
        QCOMPARE(channelNames(scene), QStringList({ "arm2", "hand", "leg", "rootdummy" }));
    }
};

QTEST_GUILESS_MAIN(TestAnimRoot)
#include "tst_animroot.moc"
