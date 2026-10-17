#ifndef VARIANTSDIALOG_H
#define VARIANTSDIALOG_H

#include <QByteArray>
#include <QDialog>
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
class QSpinBox;
class ModelViewport;

// Makes a robe's, cloak's or armour part's versions for the other bodies
// (cleanmodels variants) and shows them in a grid, one tile per race and
// phenotype, each on its body: the variant in grey, the body's base parts in
// the reference blue, so a body poking through the cloth shows. A tile can
// be remade with more or less width or length.
class VariantsDialog : public QDialog
{
    Q_OBJECT

public:
    // lookupArgs are the --game-dir / --resource-dir flags the main window
    // passes the CLI.
    VariantsDialog(const QString &cliPath, const QStringList &lookupArgs, QWidget *parent = nullptr);
    ~VariantsDialog() override;

    void setSource(const QString &mdlPath);
    void setLookupArgs(const QStringList &args) { m_lookupArgs = args; }

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
        QString output;
        QStringList warnings;
        QString error;
        double width = 1.0;
        double length = 1.0;
    };

    // One CLI run: the targets that share a width and length.
    struct Job {
        QStringList targets;
        double width = 1.0;
        double length = 1.0;
        bool overwrite = false;
    };

    void buildUi();
    void rebuildGrid();
    void updateControls();
    void selectTile(int index);
    void refreshTileStatus(Tile &tile);
    void refreshAdjustBar();
    int tileIndex(const QString &target) const;
    QString sourcePrefix() const;

    void makeVariants();
    void remakeSelected();
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

    QLabel *m_adjustTitle = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_lengthSpin = nullptr;
    QPushButton *m_remakeButton = nullptr;
    QPushButton *m_openButton = nullptr;

    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;

    QList<Tile> m_tiles;
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
