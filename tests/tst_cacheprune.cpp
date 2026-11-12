// The command line the Prune cache… button runs.

#include "cacheprune.h"

#include <QTest>

class TestCachePrune : public QObject
{
    Q_OBJECT

private slots:
    void args()
    {
        QCOMPARE(CachePrune::args("/home/me/cm cache", 30),
                 QStringList({ "cache", "prune", "--unused-for", "30d", "/home/me/cm cache" }));
        QCOMPARE(CachePrune::args("C:\\cache", 1),
                 QStringList({ "cache", "prune", "--unused-for", "1d", "C:\\cache" }));
    }

    void defaultMatchesTheCli()
    {
        // cleanmodels' own --unused-for default is 30d.
        QCOMPARE(CachePrune::unusedFor(CachePrune::DefaultDays), QString("30d"));
        QVERIFY(CachePrune::MinDays <= CachePrune::DefaultDays);
        QVERIFY(CachePrune::DefaultDays <= CachePrune::MaxDays);
    }

    void helpArgs()
    {
        QCOMPARE(CachePrune::helpArgs(), QStringList({ "cache", "prune", "--help" }));
    }

    void helpDetection()
    {
        // cleanmodels 4.18.0: cache prune --help.
        const QByteArray prune =
            "Usage: cleanmodels cache prune [flags] <cache-dir>\n\n"
            "Remove the compiled models that compile --cache-dir has not used for a while, "
            "and the files nothing refers to any more.\n\nFlags:\n"
            "  -q\talias for --quiet\n  -quiet\n    \tprint nothing but errors\n"
            "  -unused-for string\n    \tremove the models not compiled or used from the cache "
            "for this long (such as 30d or 12h) (default \"30d\")\n";
        QVERIFY(CachePrune::helpListsPrune(prune));
        // cleanmodels 4.16.0 has no cache command: it reads "cache" as a
        // file for its legacy mode and prints that mode's usage.
        const QByteArray older =
            "Usage: cleanmodels [flags] <input_path> [output_path]\n\n"
            "Legacy mode. Consider using subcommands: check, repair, compile, decompile\n\n"
            "  -tilefade-z float\n    \tZ height for tilefade slicing (default 5)\n"
            "  -workers int\n    \tparallel workers for batch mode (default 48)\n";
        QVERIFY(!CachePrune::helpListsPrune(older));
        QVERIFY(!CachePrune::helpListsPrune(QByteArray()));
    }
};

QTEST_GUILESS_MAIN(TestCachePrune)
#include "tst_cacheprune.moc"
