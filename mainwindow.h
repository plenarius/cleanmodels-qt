#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QCompleter>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QTableWidgetItem>
#include <QMainWindow>

class FileSystemModel;

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

private slots:
    void on_indirButton_released();
    void on_outdirButton_released();
    void on_cleanButton_released();
    void on_decompileCheck_stateChanged(int arg1);
    void onHelpTriggered();
    void onSaveConfigTriggered();
    void onLoadConfigTriggered();
    void onQuitTriggered();
    void onAboutTriggered();
    void updateFileListing();
    void on_cullInvisibleCheck_toggled(bool checked);
    void on_meshMergeCheck_toggled(bool checked);
    void on_forceWhiteCheck_toggled(bool checked);
    void on_placeableWithTransparencyCheck_toggled(bool checked);
    void on_animateSplotchesCheck_toggled(bool checked);
    void on_transparentBitmapKeys_editingFinished();
    void on_allowSplittingCheck_toggled(bool checked);
    void on_subObjectSpin_editingFinished();
    void on_modelClassCombo_currentIndexChanged(int index);
    void on_foliageCombo_currentIndexChanged(int index);
    void on_groundRotateTextureCombo_currentIndexChanged(int index);
    void on_tileEdgeChamfersCombo_currentIndexChanged(int index);
    void on_retileGroundPlanesCombo_currentIndexChanged(int index);
    void on_snapCombo_currentIndexChanged(int index);
    void on_snapTVertsCombo_currentIndexChanged(int index);
    void on_inDirectory_textChanged(const QString &arg1);
    void on_inDirectory_editingFinished();
    void on_outDirectory_editingFinished();
    void on_smoothingGroupsCombo_currentIndexChanged(int index);
    void on_splitFirstCombo_currentIndexChanged(int index);
    void on_repairAABBCombo_currentIndexChanged(int index);
    void on_raiseLowerCombo_currentIndexChanged(int index);
    void on_raiseLowerAmountSpin_editingFinished();
    void on_sliceForTileFadeCombo_currentIndexChanged(int index);
    void on_changeWokMatCheck_toggled(bool checked);
    void on_changeWokMatFromSpin_editingFinished();
    void on_changeWokMatToSpin_editingFinished();
    void on_moveBadPivotsCombo_currentIndexChanged(int index);
    void on_pivotsBelowZeroZCombo_currentIndexChanged(int index);
    void on_renderTrimeshCombo_currentIndexChanged(int index);
    void on_renderShadowsCombo_currentIndexChanged(int index);
    void on_repivotCombo_currentIndexChanged(int index);
    void on_waterFixupsCheck_toggled(bool checked);
    void on_waterBitmapKeys_editingFinished();
    void on_foliageBitmapKeys_editingFinished();
    void on_splotchBitmapKeys_editingFinished();
    void on_groundBitmapKeys_editingFinished();
    void on_dynamicWaterCombo_currentIndexChanged(int index);
    void on_waveHeightSpin_editingFinished();
    void on_waterRotateTextureCombo_currentIndexChanged(int index);
    void on_retileWaterCombo_currentIndexChanged(int index);
    void on_outDirectory_textChanged(const QString &arg1);
    void on_filePattern_textChanged(const QString &arg1);
    void on_filesTable_customContextMenuRequested(const QPoint &pos);
    void on_filesTable_doubleClicked(const QModelIndex &index);
    void on_rescaleLockBtn_clicked(bool checked);
    void on_rescaleXSpin_valueChanged(double arg1);
    void on_rescaleYSpin_valueChanged(double arg1);
    void on_rescaleZSpin_valueChanged(double arg1);

    void onCaptureCleanModelsOutput();
    void onCleanFinished(int, QProcess::ExitStatus);
    void copyToClipboard();

private:
    Ui::MainWindow *ui;
    FileSystemModel *m_pFileSystemModel = nullptr;
    QCompleter *m_pDirCompleter = nullptr;
    QLabel* m_pCleanStatus;
    QProcess* m_pCleanProcess;
    QProgressBar* m_pStatusProgress;
    QString m_sBinaryName;
    QString m_sBinaryPath;
    QString m_sCurrentModel;
    QString m_sInDir;
    QString m_sOutDir;
    QString m_sLastDirsPath;
    QIcon m_iconReadingMDL;
    QIcon m_iconDecompilingMDL;
    QIcon m_iconCleaningMDL;
    QIcon m_iconCleanError;
    QIcon m_iconCleanSuccess;
    QIcon m_iconCleanButton;
    QIcon m_iconAbortButton;
    QIcon m_iconDecompileButton;
    QIcon m_iconASCIIMdl;
    QIcon m_iconBinaryMdl;
    QIcon m_iconLockRescaleBtn;
    QIcon m_iconUnlockRescaleBtn;
    QElapsedTimer m_cleanTimer;
    QFileSystemWatcher m_fsWatcher;
    bool m_bCleanRunning;
    int m_nMdlsCleaned = 0;
    int m_nMdlsFailed = 0;

    void onUpdateInDir(const QString& newInDir);
    void setRescaleOption();
    void replaceUserOption(const QString& str, const QString& rpl, bool coreValue = false);
    void readInLastDirs(const QString& fileLoc);
    void readSettings();
    void writeSettings();

    void doClean();
    int findModelRow(const QString& mdlFile);
};

#endif // MAINWINDOW_H
