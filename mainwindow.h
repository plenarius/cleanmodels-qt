#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QFormLayout>
#include <QTimer>

class FileSystemModel;
class ModelViewport;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onHelpTriggered();
    void onSaveConfigTriggered();
    void onLoadConfigTriggered();
    void onQuitTriggered();
    void onAboutTriggered();
    void handleDirWatcherTimer();
    void onDirectoryContentsChanged();
    void updateFileListing();
    void onCaptureCleanModelsOutput();
    void onCleanFinished(int, QProcess::ExitStatus);
    void copyToClipboard();
    void onReportIssueTriggered();

private:
    Ui::MainWindow *ui;

    // --- I/O widgets ---
    QLineEdit *m_inDirectory;
    QLineEdit *m_outDirectory;
    QPushButton *m_indirButton;
    QPushButton *m_outdirButton;
    QLineEdit *m_filePattern;
    QComboBox *m_classificationCombo;

    // --- Mode ---
    QRadioButton *m_radioClean;
    QRadioButton *m_radioDecompile;
    QRadioButton *m_radioCompile;

    // --- Fixes ---
    QCheckBox *m_allFixesCheck;
    QWidget *m_fixesDetailWidget;
    QCheckBox *m_checkValidate;
    QCheckBox *m_checkStripDegen;
    QCheckBox *m_checkFixAnims;
    QCheckBox *m_checkRepairPivots;
    QCheckBox *m_checkFixTilefade;
    QCheckBox *m_checkTilefadeUndo;
    QCheckBox *m_checkRebuildAABB;
    QCheckBox *m_checkReparentChildren;
    QCheckBox *m_checkWrapRoot;
    QCheckBox *m_checkSplitMultiEdge;

    // --- Advanced section ---
    QWidget *m_advancedGroup;
    QDoubleSpinBox *m_scaleXSpin;
    QDoubleSpinBox *m_scaleYSpin;
    QDoubleSpinBox *m_scaleZSpin;
    QPushButton *m_scaleLockBtn;
    QComboBox *m_snapCombo;
    QComboBox *m_tvertSnapCombo;
    QComboBox *m_renderCombo;
    QComboBox *m_shadowCombo;
    QCheckBox *m_forceWhiteCheck;
    QCheckBox *m_mergeByBitmapCheck;
    QCheckBox *m_cullInvisibleCheck;

    // --- Tile section ---
    QWidget *m_tileGroup;
    QDoubleSpinBox *m_sliceHeightSpin;
    QCheckBox *m_waterEnableCheck;
    QComboBox *m_dynamicWaterCombo;
    QDoubleSpinBox *m_waveHeightSpin;
    QLineEdit *m_waterKeyEdit;
    QComboBox *m_rotateWaterCombo;
    QComboBox *m_retileWaterCombo;
    QComboBox *m_foliageCombo;
    QLineEdit *m_foliageKeyEdit;
    QCheckBox *m_animateSplotchesCheck;
    QLineEdit *m_splotchKeyEdit;
    QComboBox *m_rotateGroundCombo;
    QComboBox *m_chamferCombo;
    QComboBox *m_retileGroundCombo;
    QLineEdit *m_groundKeyEdit;
    QComboBox *m_raiseLowerCombo;
    QDoubleSpinBox *m_raiseAmountSpin;
    QCheckBox *m_remapWokMatCheck;
    QSpinBox *m_wokMatFromSpin;
    QSpinBox *m_wokMatToSpin;

    // --- Pivot section ---
    QWidget *m_pivotGroup;
    QCheckBox *m_pivotAllowSplitCheck;
    QComboBox *m_pivotBelowZ0Combo;
    QComboBox *m_pivotMoveBadCombo;
    QComboBox *m_pivotSmoothingCombo;
    QSpinBox *m_pivotMinFacesSpin;
    QComboBox *m_pivotSplitFirstCombo;

    // --- Placeable transparency ---
    QCheckBox *m_placeableTransCheck;
    QLineEdit *m_transparencyKeyEdit;

    // --- Viewport controls ---
    QCheckBox *m_wireframeCheck;
    QCheckBox *m_gridCheck;
    QCheckBox *m_refModelCheck;
    QComboBox *m_refModelCombo;
    QPushButton *m_refBrowseBtn;

    // --- Layout structure ---
    QWidget *m_sidebarWidget = nullptr;
    QSplitter *m_mainSplitter = nullptr;
    QTextBrowser *m_detailPanel = nullptr;
    QPushButton *m_sidebarToggleBtn = nullptr;
    QSplitter *m_tableDetailSplitter = nullptr;

    // --- Action / table / log ---
    QPushButton *m_cleanButton;
    QTableWidget *m_filesTable;
    QTextBrowser *m_debugTextBrowser;
    QLabel *m_mdlsDetectedLabel;
    QLabel *m_mdlsCleanedLabel;
    QLabel *m_mdlsFailedLabel;

    // --- Raw log drawer ---
    QWidget *m_rawLogDrawer = nullptr;
    bool m_rawLogVisible = false;
    QString m_batchSummary;

    // --- Per-file detail results ---
    QMap<QString, QStringList> m_fileResults;

    // --- Infrastructure ---
    FileSystemModel *m_pFileSystemModel = nullptr;
    QCompleter *m_pDirCompleter = nullptr;
    QLabel *m_pCleanStatus;
    QProcess *m_pCleanProcess;
    QProgressBar *m_pStatusProgress;
    QString m_sBinaryName;
    QString m_sBinaryPath;
    QString m_sCurrentModel;
    QString m_sInDir;
    QString m_sOutDir;
    QIcon m_iconCleanButton;
    QIcon m_iconAbortButton;
    QIcon m_iconDecompileButton;
    QElapsedTimer m_cleanTimer;
    QFileSystemWatcher m_fsWatcher;
    QTimer *m_dirWatcherTimer;
    QByteArray m_stdoutBuffer;
    bool m_bFilesHaveChanged = false;
    bool m_bUpdateFilesAfterClean = false;
    bool m_bCleanRunning = false;
    int m_nMdlsCleaned = 0;
    int m_nMdlsFailed = 0;

    ModelViewport *m_viewport = nullptr;

    // --- Report issue state ---
    QStringList m_lastFailedFiles;
    QString m_lastErrorOutput;
    QString m_lastCommand;

    // --- Methods ---
    void buildUi();
    void updateModeUI();
    static QFormLayout *makeStandardForm(int spacing = 4);
    QWidget *createCollapsibleGroup(const QString &title, QWidget **contentOut, bool startCollapsed = true);
    void onUpdateInDir(const QString &newInDir);
    void populateRefModelCombo();
    void readSettings();
    void writeSettings();
    void saveSettings();
    void loadSettings();
    void doClean();
    QStringList buildCliArgs();
    int findModelRow(const QString &mdlFile);
    void appendDebugHtml(const QString &html);
    void toggleSidebar();
    void toggleRawLog();
    void showFileDetails(const QString &fileName);
    void showBatchSummary();
    QStringList collectTableFilePaths(bool allRows) const;
    void reportIssue(const QStringList &files, const QString &errorOutput, const QString &command);
    void reportIssueInteractive(const QStringList &files);
    static QString humanFileSize(qint64 bytes);
};

#endif // MAINWINDOW_H
