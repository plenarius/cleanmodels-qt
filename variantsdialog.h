#ifndef VARIANTSDIALOG_H
#define VARIANTSDIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

class QCheckBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class ModelViewport;

// Makes a robe's, cloak's or armour part's versions for the other bodies
// (cleanmodels variants) and shows them in a grid, one tile per race and
// phenotype shape (the CLI's --list-bodies: custom races from the haks
// included; phenotypes of the same shape share a tile), each on its body: the variant in grey, the body's base parts in
// the reference blue, so a body poking through the cloth shows. A tile can
// be remade with more or less width or length.
class VariantsDialog : public QDialog
{
    Q_OBJECT

    // A tile's adjustments in percent by name (width, length, shoulders,
    // chest, waist, hips, hem); a name not in it is 100.
    using Adjust = QMap<QString, int>;

public:
    // lookupArgs are the --game-dir / --resource-dir flags the main window
    // passes the CLI.
    VariantsDialog(const QString &cliPath, const QStringList &lookupArgs, QWidget *parent = nullptr);
    ~VariantsDialog() override;

    void setSource(const QString &mdlPath);
    // Sets the --game-dir / --resource-dir flags; a change lists the bodies
    // again (custom races come from the resource folders).
    void setLookupArgs(const QStringList &args);

signals:
    // The user asked to see a written variant in the main viewport.
    void openInViewport(const QString &mdlPath);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    struct Tile {
        QString target;           // body prefix, pmd0
        QString title;            // "Dwarf, normal"
        bool isSource = false;
        QFrame *frame = nullptr;
        QCheckBox *check = nullptr;
        QLabel *titleLabel = nullptr;
        ModelViewport *view = nullptr;
        QLabel *status = nullptr;
        QStringList members;      // bodies of the same shape: this fit, renamed
        QList<int> memberPhenos;
        int written = 0;          // files written for the tile in the last run
        QString output;
        QStringList warnings;
        QString error;
        Adjust adjust;            // the tile's adjustments
    };

    // One CLI run: the targets that share their adjustments.
    struct Job {
        QStringList targets;
        Adjust adjust;
        bool overwrite = false;
    };

    void buildUi();
    void clearGrid();
    void rebuildGrid();
    void buildTiles(const QJsonArray &bodies);
    void updateControls();
    void selectTile(int index);
    void refreshTileStatus(Tile &tile);
    void refreshAdjustBar();
    int tileIndex(const QString &target) const;
    QString sourcePrefix() const;

    void makeVariants();
    void remakeSelected();
    void remakeTile(int index);
    void scheduleRemake();
    void setAdjust(const QString &name, int percent);
    void startNextJob();
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void handleEvent(const QJsonObject &evt);

    QString m_cliPath;
    QStringList m_lookupArgs;
    QTemporaryDir m_previewDir;

    QLineEdit *m_sourceEdit = nullptr;
    QLineEdit *m_outDirEdit = nullptr;
    QCheckBox *m_compileCheck = nullptr;
    QCheckBox *m_overwriteCheck = nullptr;
    QCheckBox *m_syncViewsCheck = nullptr;
    QPushButton *m_makeButton = nullptr;
    QGridLayout *m_grid = nullptr;
    QWidget *m_gridHost = nullptr;
    QLabel *m_gridNote = nullptr;

    QWidget *m_adjustBox = nullptr;
    QLabel *m_adjustTitle = nullptr;
    QLabel *m_notesLabel = nullptr;
    QMap<QString, QSlider *> m_sliders;
    QMap<QString, QSpinBox *> m_spins;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_remakeButton = nullptr;
    QList<int> m_pendingRemakes; // tiles to remake when the run going on ends
    QTimer *m_remakeTimer = nullptr; // a short wait after a change, then remake
    QPushButton *m_openButton = nullptr;

    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;

    QList<Tile> m_tiles;
    QHash<QString, int> m_tileOf; // body prefix -> tile (members too)
    QProcess *m_listProcess = nullptr;
    QString m_gridSource; // the model the grid was built for
    int m_selected = -1;

    QProcess *m_process = nullptr;
    bool m_stopping = false; // the user stopped the run (Stop, or closing)
    QByteArray m_stdout;
    QByteArray m_stderr;
    QList<Job> m_jobs;
    int m_jobTotal = 0;
    int m_jobDone = 0;
    int m_written = 0;
    int m_failed = 0;
};

#endif // VARIANTSDIALOG_H
