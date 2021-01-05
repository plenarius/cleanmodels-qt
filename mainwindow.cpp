#include "fsmodel.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressBar>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QWhatsThis>
#include <QWindow>

using namespace std;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->debugTextBrowser->insertHtml(tr("Welcome to Clean Models:EE QT!<br>"));

#ifdef Q_OS_WIN
    m_sBinaryName = "cleanmodels-cli.exe";
#else
    m_sBinaryName = "cleanmodels-cli";
#endif

    bool cliFound = true;
    auto cliInPath = QStandardPaths::findExecutable(m_sBinaryName);
    if (cliInPath.isEmpty())
    {
        QStringList cliPaths = {QDir::currentPath(), QApplication::applicationDirPath()};
        cliInPath = QStandardPaths::findExecutable(m_sBinaryName, cliPaths);
        if (cliInPath.isEmpty())
            cliFound = false;
    }

    if (cliFound)
    {
        m_sBinaryPath = cliInPath;
        QString foundMsg = "Clean Models Command Line Interface found at " % m_sBinaryPath;
        ui->debugTextBrowser->insertHtml(tr(foundMsg.toStdString().c_str()));
        auto sb = ui->debugTextBrowser->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
    else
    {
        QString errorMsg = "Could not find the " % m_sBinaryName % " executable in the current directory or in your path!";
        QMessageBox::critical(nullptr, "No cleanmodels-cli", tr(errorMsg.toStdString().c_str()));
        ui->debugTextBrowser->insertHtml(tr(errorMsg.toStdString().c_str()));
        auto sb = ui->debugTextBrowser->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

    m_pCleanProcess = new QProcess(this);
    m_bCleanRunning = false;
    m_sLastDirsPath = QCoreApplication::applicationDirPath() % "/last_dirs.pl";
    bool fileExists = QFileInfo::exists(m_sLastDirsPath) && QFileInfo(m_sLastDirsPath).isFile();
    if (!fileExists)
    {
        QFile fromResource(":/last_dirs.pl");
        fromResource.copy(m_sLastDirsPath);
        QFile out(m_sLastDirsPath);
        out.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteOwner | QFileDevice::WriteGroup);
    }

    auto* sStatusLabel = new QLabel( QString( tr("Status:") ) );
    m_pCleanStatus = new QLabel( QString( tr("Idle") ) );
    m_pStatusProgress = new QProgressBar();
    m_pStatusProgress->setRange(0, 0);
    m_pStatusProgress->setTextVisible(false);
    m_pStatusProgress->setVisible(false);
    m_pStatusProgress->setMaximumHeight(12);
    m_pStatusProgress->setMaximumWidth(100);

    statusBar()->addPermanentWidget( m_pStatusProgress );
    statusBar()->addPermanentWidget( sStatusLabel );
    statusBar()->addPermanentWidget( m_pCleanStatus );

    m_pDirCompleter = new QCompleter(this);
    m_pDirCompleter->setMaxVisibleItems(4);
    m_pFileSystemModel = new FileSystemModel(m_pDirCompleter);
    m_pFileSystemModel->setFilter(QDir::Dirs|QDir::Drives|QDir::NoDotAndDotDot|QDir::AllDirs);
    m_pDirCompleter->setModel(m_pFileSystemModel);
    ui->inDirectory->setCompleter(m_pDirCompleter);
    ui->outDirectory->setCompleter(m_pDirCompleter);

    ui->filesTable->setColumnCount(5);
    ui->filesTable->setColumnWidth(1, 100);
    ui->filesTable->setColumnWidth(2, 140);
    ui->filesTable->setColumnWidth(3, 70);
    ui->filesTable->setColumnWidth(4, 100);
    ui->filesTable->setHorizontalHeaderLabels({"File", "Size", "Status", "Fixes", "Time"});
    ui->filesTable->setAlternatingRowColors(true);
    ui->filesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->filesTable->horizontalHeader()->setVisible(true);
    ui->filesTable->verticalHeader()->setDefaultSectionSize(20);
    ui->filesTable->verticalHeader()->setVisible(false);
    ui->filesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->filesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->filesTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_iconReadingMDL = QIcon(":icons/reading-mdl");
    m_iconDecompilingMDL = QIcon(":icons/decompiling-mdl");
    m_iconCleaningMDL = QIcon(":icons/cleaning-mdl");
    m_iconCleanSuccess = QIcon(":icons/clean-success");
    m_iconCleanError = QIcon(":icons/clean-error");
    m_iconCleanButton = QIcon(":icons/clean-button");
    m_iconASCIIMdl = QIcon(":icons/mdl-ascii");
    m_iconBinaryMdl = QIcon(":icons/mdl-binary");
    m_iconAbortButton = QIcon(":icons/abort-button");
    m_iconDecompileButton = QIcon(":icons/decompile-button");
    m_iconLockRescaleBtn = QIcon(":icons/lock-rescale");
    m_iconUnlockRescaleBtn = QIcon(":icons/unlock-rescale");
    ui->actionLoadPreset->setIcon(QIcon(":icons/load-preset"));
    ui->actionSavePreset->setIcon(QIcon(":icons/save-preset"));
    ui->actionHelp->setIcon(QIcon(":icons/whats-this"));
    ui->indirButton->setIcon(QIcon(":icons/indir"));
    ui->outdirButton->setIcon(QIcon(":icons/outdir"));
    ui->cleanButton->setIcon(m_iconCleanButton);

    m_dirWatcherTimer = new QTimer(this);
    m_dirWatcherTimer->setInterval(500);
    connect(m_dirWatcherTimer, &QTimer::timeout, this, QOverload<>::of(&MainWindow::handleDirWatcherTimer));
    m_bUpdateFilesAfterClean = false;

    readInLastDirs(m_sLastDirsPath);
    QObject::connect(m_pCleanProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &MainWindow::onCleanFinished);
    QObject::connect(ui->actionHelp, SIGNAL(triggered()), this, SLOT(onHelpTriggered()));
    QObject::connect(ui->actionAbout, SIGNAL(triggered()), this, SLOT(onAboutTriggered()));
    QObject::connect(ui->actionSavePreset, SIGNAL(triggered()), this, SLOT(onSaveConfigTriggered()));
    QObject::connect(ui->actionLoadPreset, SIGNAL(triggered()), this, SLOT(onLoadConfigTriggered()));
    QObject::connect(ui->actionQuit, SIGNAL(triggered()), this, SLOT(onQuitTriggered()));
    QObject::connect(&m_fsWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(onDirectoryContentsChanged()));
    readSettings();
}

MainWindow::~MainWindow()
{
    delete ui;
    m_pCleanProcess->close();
    delete m_pCleanProcess;
}

// Window Position/Geometry
void MainWindow::readSettings()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (const QWindow *window = windowHandle())
        screen = window->screen();
    if (!screen)
        return;

    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    const QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (geometry.isEmpty())
    {
        const QRect availableGeometry = screen->availableGeometry();
        resize(availableGeometry.width() / 3, availableGeometry.height() / 2);
        move((availableGeometry.width() - width()) / 2,
             (availableGeometry.height() - height()) / 2);
    }
    else
    {
        restoreGeometry(geometry);
    }
}

void MainWindow::writeSettings()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent*)
{
    writeSettings();
}

// Main last_dirs parsing and writing functions
void MainWindow::readInLastDirs(const QString& fileLoc)
{
    QFile inputFile(fileLoc);
    if (inputFile.open(QIODevice::ReadOnly))
    {
       QTextStream in(&inputFile);
       while (!in.atEnd())
       {
          QString line = in.readLine();
          QString str = R"(^:-asserta\((.*)\((.*)[,]?(\w+|\[.*\])?\)\)\.$)";

          QRegularExpression re(str, QRegularExpression::InvertedGreedinessOption | QRegularExpression::MultilineOption);
          QRegularExpressionMatchIterator i = re.globalMatch(line);
          while (i.hasNext())
          {
              QRegularExpressionMatch match = i.next();
              QString captured = match.captured(1);
              QString capturedTwo = match.captured(2).isEmpty() ? match.captured(3) : match.captured(2);
              if (captured.isEmpty() || capturedTwo.isEmpty())
                  break;
              if (capturedTwo.startsWith("'"))
                  capturedTwo.remove(0,1);
              if (capturedTwo.endsWith("'"))
                  capturedTwo.chop(1);
              if (captured == "g_indir")
              {
                  onUpdateInDir(capturedTwo);
                  QDir absDir;
                  m_pFileSystemModel->setRootPath(absDir.absoluteFilePath(capturedTwo));
              }
              else if (captured == "g_outdir")
              {
                  m_sOutDir = capturedTwo;
                  ui->outDirectory->setText(m_sOutDir);
                  QDir absDir;
                  m_pFileSystemModel->setRootPath(absDir.absoluteFilePath(m_sOutDir));
              }
              else if (captured == "g_pattern")
              {
                  ui->filePattern->setText(capturedTwo);
              }
              else if (captured == "g_logfile")
              {
                  ui->logFileName->setText(capturedTwo);
              }
              else if (captured == "g_small_log")
              {
                  ui->summaryLogFileName->setText(capturedTwo);
              }
              else if (captured == "g_user_option")
              {
                  QString value = match.captured(3);
                  if (capturedTwo == "classification")
                  {
                      if (value == "character")
                          ui->modelClassCombo->setCurrentIndex(1);
                      else if (value == "door")
                          ui->modelClassCombo->setCurrentIndex(2);
                      else if (value == "effect")
                          ui->modelClassCombo->setCurrentIndex(3);
                      else if (value == "item")
                          ui->modelClassCombo->setCurrentIndex(4);
                      else if (value == "tile")
                          ui->modelClassCombo->setCurrentIndex(5);
                      else
                          ui->modelClassCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "snap")
                  {
                      if (value == "binary")
                          ui->snapCombo->setCurrentIndex(1);
                      else if (value == "decimal")
                          ui->snapCombo->setCurrentIndex(2);
                      else if (value == "fine")
                          ui->snapCombo->setCurrentIndex(3);
                      else
                          ui->snapCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "tvert_snap")
                  {
                      if (value == "256")
                          ui->snapTVertsCombo->setCurrentIndex(1);
                      else if (value == "512")
                          ui->snapTVertsCombo->setCurrentIndex(2);
                      else if (value == "1024")
                          ui->snapTVertsCombo->setCurrentIndex(3);
                      else
                          ui->snapTVertsCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "use_Smoothed")
                  {
                      if (value == "ignore")
                          ui->smoothingGroupsCombo->setCurrentIndex(1);
                      else if (value == "protect")
                          ui->smoothingGroupsCombo->setCurrentIndex(2);
                      else
                          ui->smoothingGroupsCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "fix_overhangs")
                  {
                      if (value == "yes")
                          ui->repairAABBCombo->setCurrentIndex(1);
                      else if (value == "interior_only")
                          ui->repairAABBCombo->setCurrentIndex(2);
                      else
                          ui->repairAABBCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "dynamic_water")
                  {
                      if (value == "no")
                          ui->dynamicWaterCombo->setCurrentIndex(1);
                      else if (value == "wavy")
                          ui->dynamicWaterCombo->setCurrentIndex(2);
                      else
                          ui->dynamicWaterCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "rotate_water")
                  {
                      if (value == "1")
                          ui->waterRotateTextureCombo->setCurrentIndex(1);
                      else if (value == "0")
                          ui->waterRotateTextureCombo->setCurrentIndex(2);
                      else
                          ui->waterRotateTextureCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "tile_water")
                  {
                      if (value == "1")
                          ui->retileWaterCombo->setCurrentIndex(1);
                      else if (value == "2")
                          ui->retileWaterCombo->setCurrentIndex(2);
                      else if (value == "3")
                          ui->retileWaterCombo->setCurrentIndex(3);
                      else
                          ui->retileWaterCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "tile_raise")
                  {
                      if (value == "raise")
                          ui->raiseLowerCombo->setCurrentIndex(1);
                      else if (value == "lower")
                          ui->raiseLowerCombo->setCurrentIndex(2);
                      else
                          ui->raiseLowerCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "slice")
                  {
                      if (value == "no")
                          ui->sliceForTileFadeCombo->setCurrentIndex(1);
                      else if (value == "undo")
                          ui->sliceForTileFadeCombo->setCurrentIndex(2);
                      else
                          ui->sliceForTileFadeCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "tile_raise_amount")
                  {
                      ui->raiseLowerAmountSpin->setValue(value.toDouble());
                  }
                  else if (capturedTwo == "render")
                  {
                      if (value == "all")
                          ui->renderTrimeshCombo->setCurrentIndex(1);
                      else if (value == "none")
                          ui->renderTrimeshCombo->setCurrentIndex(2);
                      else
                          ui->renderTrimeshCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "shadow")
                  {
                      if (value == "all")
                          ui->renderShadowsCombo->setCurrentIndex(1);
                      else if (value == "none")
                          ui->renderShadowsCombo->setCurrentIndex(2);
                      else
                          ui->renderShadowsCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "repivot")
                  {
                      if (value == "all")
                          ui->repivotCombo->setCurrentIndex(1);
                      else if (value == "none")
                          ui->repivotCombo->setCurrentIndex(2);
                      else
                          ui->repivotCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "pivots_below_z=0")
                  {
                      if (value == "allow")
                          ui->pivotsBelowZeroZCombo->setCurrentIndex(1);
                      else if (value == "slice")
                          ui->pivotsBelowZeroZCombo->setCurrentIndex(2);
                      else
                          ui->pivotsBelowZeroZCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "move_bad_pivots")
                  {
                      if (value == "top")
                          ui->moveBadPivotsCombo->setCurrentIndex(1);
                      else if (value == "middle")
                          ui->moveBadPivotsCombo->setCurrentIndex(2);
                      else if (value == "bottom")
                          ui->moveBadPivotsCombo->setCurrentIndex(3);
                      else
                          ui->moveBadPivotsCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "foliage")
                  {
                      if (value == "tilefade")
                          ui->foliageCombo->setCurrentIndex(1);
                      else if (value == "animate")
                          ui->foliageCombo->setCurrentIndex(2);
                      else if (value == "de-animate")
                          ui->foliageCombo->setCurrentIndex(3);
                      else if (value == "ignore")
                          ui->foliageCombo->setCurrentIndex(4);
                      else
                          ui->foliageCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "rotate_ground")
                  {
                      if (value == "1")
                          ui->groundRotateTextureCombo->setCurrentIndex(1);
                      else if (value == "0")
                          ui->groundRotateTextureCombo->setCurrentIndex(2);
                      else
                          ui->groundRotateTextureCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "chamfer")
                  {
                      if (value == "add")
                          ui->tileEdgeChamfersCombo->setCurrentIndex(1);
                      else if (value == "delete")
                          ui->tileEdgeChamfersCombo->setCurrentIndex(2);
                      else
                          ui->tileEdgeChamfersCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "tile_ground")
                  {
                      if (value == "1")
                          ui->retileGroundPlanesCombo->setCurrentIndex(1);
                      else if (value == "2")
                          ui->retileGroundPlanesCombo->setCurrentIndex(2);
                      else if (value == "3")
                          ui->retileGroundPlanesCombo->setCurrentIndex(3);
                      else
                          ui->retileGroundPlanesCombo->setCurrentIndex(0);
                  }
                  else if (capturedTwo == "invisible_mesh_cull")
                  {
                      ui->cullInvisibleCheck->setChecked(value == "yes");
                  }
                  else if (capturedTwo == "map_aabb_material")
                  {
                      ui->changeWokMatCheck->setChecked(value == "yes");
                      ui->changeWokMatGroupBox->setEnabled(value == "yes");
                  }
                  else if (capturedTwo == "allow_split")
                  {
                      ui->allowSplittingCheck->setChecked(value == "yes");
                  }
                  else if (capturedTwo == "do_water")
                  {
                      ui->waterFixupsCheck->setChecked(value == "yes");
                      ui->waterFrame->setEnabled(value == "yes");
                  }
                  else if (capturedTwo == "water_key")
                  {
                      ui->waterBitmapKeys->setText(value);
                  }
                  else if (capturedTwo == "ground_key")
                  {
                      ui->groundBitmapKeys->setText(value);
                  }
                  else if (capturedTwo == "splotch_key")
                  {
                      ui->splotchBitmapKeys->setText(value);
                  }
                  else if (capturedTwo == "foliage_key")
                  {
                      ui->foliageBitmapKeys->setText(value);
                  }
                  else if (capturedTwo == "min_Size")
                  {
                     ui->subObjectSpin->setValue(value.toInt());
                  }
                  else if (capturedTwo == "merge_by_bitmap")
                  {
                      ui->meshMergeCheck->setChecked(value == "yes");
                  }
                  else if (capturedTwo == "placeable_with_transparency")
                  {
                      ui->placeableWithTransparencyCheck->setChecked(value == "yes");
                      ui->transBitmapKeyFrame->setEnabled(value == "yes");
                  }
                  else if (capturedTwo == "splotch")
                  {
                      ui->animateSplotchesCheck->setChecked(value == "animate");
                      ui->splotchBitmapKeysLabel->setEnabled(value == "animate");
                      ui->splotchBitmapKeys->setEnabled(value == "animate");
                  }
                  else if (capturedTwo == "force_white")
                  {
                      ui->forceWhiteCheck->setChecked(value == "yes");
                  }
                  else if (capturedTwo == "split_Priority")
                  {
                      ui->forceWhiteCheck->setChecked(value == "concave");
                  }
                  else if (capturedTwo == "transparency_key")
                  {
                      ui->transparentBitmapKeys->setText(value);
                  }
                  else if (capturedTwo == "wave_height")
                  {
                      ui->waveHeightSpin->setValue(value.toDouble());
                  }
                  else if (capturedTwo == "map_aabb_from")
                  {
                      ui->changeWokMatFromSpin->setValue(value.toInt());
                  }
                  else if (capturedTwo == "map_aabb_to")
                  {
                      ui->changeWokMatToSpin->setValue(value.toInt());
                  }
                  else if (capturedTwo == "rescaleXYZ")
                  {
                      double X = 1.0;
                      double Y = 1.0;
                      double Z = 1.0;
                      if (value != "no")
                      {
                          auto scales = value.mid(1,value.length() - 2).split(",");
                          X = scales[0].toDouble(nullptr);
                          Y = scales[1].toDouble(nullptr);
                          Z = scales[2].toDouble(nullptr);
                      }
                      ui->rescaleXSpin->setValue(X);
                      ui->rescaleYSpin->setValue(Y);
                      ui->rescaleZSpin->setValue(Z);
                  }
              }
          }
       }
       inputFile.close();
    }
}

void MainWindow::replaceUserOption(const QString& key, const QString& value, bool coreVal)
{
    QString str, rpl;
    if (!coreVal)
    {
        str = "^:-asserta\\(g_user_option\\(" % key % R"(,(.*)\)\)\.$)";
        rpl = ":-asserta(g_user_option(" % key % "," % value % ")).";
    }
    else
    {
        str = "^:-asserta\\(" % key % R"((.*)\)\)\.$)";
        rpl = ":-asserta(" % key % "('" % value % "')).";
    }
    QFile file(m_sLastDirsPath);
    if(!file.open(QIODevice::Text | QIODevice::ReadWrite))
    {
        QMessageBox::critical(nullptr, "exception", tr("Could not open last_dirs for saving!"));
        return;
    }
    QString dataText = file.readAll();
    file.close();

    QRegularExpression re(str, QRegularExpression::InvertedGreedinessOption | QRegularExpression::MultilineOption);
    dataText.replace(re, rpl);

    if(file.open(QFile::WriteOnly | QFile::Truncate))
    {
        QTextStream out(&file);
        out << dataText;
    }
    file.close();
}

// SLOTS/SIGNALS
void MainWindow::onLoadConfigTriggered()
{
    QFileDialog fileDialog;
    fileDialog.setAcceptMode(QFileDialog::AcceptMode::AcceptOpen);
    QStringList nameFilters;
    nameFilters.append("Clean Models Config (*.cm)");
    fileDialog.setNameFilters(nameFilters);
    fileDialog.setDirectory(QDir::currentPath());
    if (fileDialog.exec())
    {
        QString fileName = fileDialog.selectedFiles()[0];
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::information(this, tr("Unable to open file"), file.errorString());
            return;
        }
        else
            file.close();
        if (QFile::exists(m_sLastDirsPath))
        {
            QFile::remove(m_sLastDirsPath);
        }
        if (!file.copy(m_sLastDirsPath))
        {
            QMessageBox::information(this, tr("Unable to load file"), file.errorString());
            return;
        }
        else
            readInLastDirs(m_sLastDirsPath);
    }
}

void MainWindow::onSaveConfigTriggered()
{
    QFileDialog fileDialog;
    fileDialog.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);
    fileDialog.setFileMode(QFileDialog::AnyFile);
    fileDialog.setDefaultSuffix("cm");
    QStringList nameFilters;
    nameFilters.append("Clean Models Config (*.cm)");
    fileDialog.setNameFilters(nameFilters);
    fileDialog.setDirectory(QDir::currentPath());
    if (fileDialog.exec())
    {
        QString fileName = fileDialog.selectedFiles()[0];
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly))
        {
            QMessageBox::information(this, tr("Unable to open file"), file.errorString());
            return;
        }
        if (QFile::exists(fileName))
        {
            QFile::remove(fileName);
        }
        QFile fromResource(m_sLastDirsPath);
        if (fromResource.copy(fileName))
        {
            QFile out(fileName);
            out.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteOwner | QFileDevice::WriteGroup);
            file.close();
        }
        else
        {
            QMessageBox::information(this, tr("Unable to save file"),fromResource.errorString());
            return;
        }
    }
}

void MainWindow::onAboutTriggered()
{
    QMessageBox::about(this, tr("About Clean Models:EE QT"),
                       tr("A front end to Clean Models, a utility to tidy up 3d models\n"
                          "for usage in Neverwinter Nights: Enhanced Edition."));
}

void MainWindow::onHelpTriggered()
{
    QWhatsThis::enterWhatsThisMode();
}

void MainWindow::onQuitTriggered()
{
    if (m_bCleanRunning)
        m_pCleanProcess->kill();

    QApplication::quit();
}

void MainWindow::copyToClipboard()
{
    auto *selectedFile = ui->filesTable->selectedItems()[0];
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(m_sInDir % "/" % selectedFile->text());
}

void MainWindow::on_cleanButton_released()
{
    doClean();
}

void MainWindow::on_decompileCheck_stateChanged(int arg1)
{
    if (arg1 == Qt::Unchecked)
    {
        ui->cleanButton->setText(tr("Clean"));
        ui->cleanButton->setIcon(m_iconCleanButton);
        ui->mdlsCleanedLabel->setText(tr("Files Cleaned: 0"));
        ui->classSnapBox->setDisabled(false);
        ui->tilesTab->setDisabled(false);
        ui->coreFixesBox->setDisabled(false);
        ui->pivotFrame->setDisabled(false);
        ui->rescaleFrame->setDisabled(false);
    }
    else
    {
        ui->cleanButton->setText(tr("Decompile"));
        ui->cleanButton->setIcon(m_iconDecompileButton);
        ui->mdlsCleanedLabel->setText(tr("Files Decompiled: 0"));
        ui->classSnapBox->setDisabled(true);
        ui->tilesTab->setDisabled(true);
        ui->coreFixesBox->setDisabled(true);
        ui->pivotFrame->setDisabled(true);
        ui->rescaleFrame->setDisabled(true);
    }
}

void MainWindow::onDirectoryContentsChanged()
{
    m_dirWatcherTimer->stop();
    m_bFilesHaveChanged = true;
    m_dirWatcherTimer->start();
}

void MainWindow::handleDirWatcherTimer()
{
    if (m_bFilesHaveChanged)
    {
        m_bFilesHaveChanged = false;
        if (m_bCleanRunning)
        {
            m_bUpdateFilesAfterClean = true;
        }
        else
            updateFileListing();
    }
}

void MainWindow::updateFileListing()
{
    ui->filesTable->setRowCount(0);
    auto pattern = ui->filePattern->text();
    QDir dir(ui->inDirectory->text());
    dir.setNameFilters((QStringList(pattern)));
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
    QStringList totalfiles = dir.entryList();
    ui->mdlsDetectedLabel->setText(tr("Files detected: ") % QString::number(totalfiles.count()));
    if (!ui->decompileCheck->isChecked())
        ui->mdlsCleanedLabel->setText(tr("Files Cleaned: 0"));
    else
        ui->mdlsCleanedLabel->setText(tr("Files Decompiled: 0"));
    ui->mdlsFailedLabel->setText(tr("Failures: 0"));

    for (const QString &filePath : totalfiles)
    {
        QFile inputFile(ui->inDirectory->text() % "/" % filePath);
        QTextStream stream(&inputFile);
        inputFile.open(QIODevice::ReadOnly);
        if (!inputFile.isOpen())
            continue;
        auto line = stream.readLine().trimmed().toStdString();
        inputFile.close();
        auto isASCII = std::all_of(line.begin(), line.end(), ::isprint);
        auto *fileNameItem = new QTableWidgetItem(filePath);
        fileNameItem->setIcon(isASCII ? m_iconASCIIMdl : m_iconBinaryMdl);
        fileNameItem->setToolTip(isASCII ? tr("ASCII MDL") : tr("Binary MDL"));
        auto *fileSizeItem = new QTableWidgetItem();
        fileSizeItem->setText(QString::number(inputFile.size()));
        auto *fixesItem = new QTableWidgetItem("0");
        fixesItem->setTextAlignment(Qt::AlignCenter);
        auto *timerItem = new QTableWidgetItem("00:00.000");
        timerItem->setTextAlignment(Qt::AlignCenter);
        int row = ui->filesTable->rowCount();
        ui->filesTable->insertRow(row);
        ui->filesTable->setItem(row, 0, fileNameItem);
        ui->filesTable->setItem(row, 1, fileSizeItem);
        ui->filesTable->setItem(row, 3, fixesItem);
        ui->filesTable->setItem(row, 4, timerItem);
    }
}

void MainWindow::onUpdateInDir(const QString& newInDir)
{
    m_dirWatcherTimer->stop();
    if (!m_sInDir.isEmpty())
        m_fsWatcher.removePath(m_sInDir);
    m_bFilesHaveChanged = false;
    m_fsWatcher.addPath(newInDir);
    ui->inDirectory->setText(newInDir);
    replaceUserOption("g_indir", newInDir, true);
    m_sInDir = newInDir;
    updateFileListing();
    m_dirWatcherTimer->start();
}

void MainWindow::on_indirButton_released()
{
    QFileDialog dialog(this);
    QStringList inDirectory;
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly,true);
    dialog.setDirectory(m_sInDir);
    dialog.setLabelText(QFileDialog::Accept, tr("Set"));
    if ( dialog.exec() )
    {
        inDirectory = dialog.selectedFiles();
        onUpdateInDir(inDirectory.at(0));
    }
}

void MainWindow::on_inDirectory_textChanged(const QString &arg1)
{
    const QFileInfo inputDir(arg1);
    QDir dir(QDir::currentPath());
    QString s, f;
    s = dir.relativeFilePath(arg1);
    f = dir.absoluteFilePath(arg1);
    if ((!inputDir.exists()) || (!inputDir.isDir()) || (!inputDir.isWritable()))
    {
        if (QFile(s).exists())
        {
            ui->inDirectory->setStatusTip(tr("Input folder resolved as ") %f);
            ui->inDirectory->setStyleSheet("color: #000000");
        }
        else
            ui->inDirectory->setStyleSheet("color: #FF0000");
    }
    else
    {
        ui->inDirectory->setStatusTip(tr("Input folder resolved as ") %f);
        ui->inDirectory->setStyleSheet("color: #000000");
    }
}

void MainWindow::on_inDirectory_editingFinished()
{
    const QFileInfo outputDir(ui->inDirectory->text());
    if ((!outputDir.exists()) || (!outputDir.isDir()) || (!outputDir.isWritable()))
    {
        ui->inDirectory->setStyleSheet("color: #FF0000");
        ui->cleanButton->setEnabled(false);
        ui->cleanButton->setToolTip(tr("Action disabled until valid input directory set."));
    }
    else
    {
        ui->inDirectory->setStyleSheet("color: #000000");
        ui->cleanButton->setEnabled(true);
        ui->cleanButton->setToolTip(tr("Perform action."));
        onUpdateInDir(ui->inDirectory->text());
    }
}

void MainWindow::on_outdirButton_released()
{
    QFileDialog dialog(this);
    QStringList outDirectory;
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly,true);
    dialog.setDirectory(m_sOutDir);
    dialog.setLabelText(QFileDialog::Accept, tr("Set"));
    if ( dialog.exec() )
    {
        outDirectory = dialog.selectedFiles();
        ui->outDirectory->setText(outDirectory.at(0));
        m_sOutDir = ui->outDirectory->text();
        replaceUserOption("g_outdir", m_sOutDir, true);
    }
}

void MainWindow::on_outDirectory_textChanged(const QString &arg1)
{
    const QFileInfo outputDir(arg1);
    QDir dir(QDir::currentPath());
    ui->outDirectory->setStatusTip(tr("Output folder resolved as ") % dir.absoluteFilePath(arg1));
}

void MainWindow::on_outDirectory_editingFinished()
{
    m_sOutDir = ui->outDirectory->text();
    const QFileInfo outputDir(m_sOutDir);
    QDir dir(QDir::currentPath());
    QString f = dir.absoluteFilePath(m_sOutDir);
    replaceUserOption("g_outdir", m_sOutDir, true);
    ui->outDirectory->setStatusTip(tr("Output folder resolved as ") % f);
}

void MainWindow::on_filePattern_textChanged(const QString &pattern)
{
    m_dirWatcherTimer->stop();
    m_bFilesHaveChanged = false;
    replaceUserOption("g_pattern", pattern, true);
    updateFileListing();
    m_dirWatcherTimer->start();
}

void MainWindow::on_modelClassCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("classification","character");
        break;
    case 2:
        replaceUserOption("classification","door");
        break;
    case 3:
        replaceUserOption("classification","effect");
        break;
    case 4:
        replaceUserOption("classification","item");
        break;
    case 5:
        replaceUserOption("classification","tile");
        break;
    case 0:
    default:
        replaceUserOption("classification","automatic");
        break;
    }
    if (!index || index == 5)
    {
        if (ui->mainTabs->count() == 1)
            ui->mainTabs->addTab(ui->tilesTab, "Tiles");
    }
    else
    {
        if (ui->mainTabs->count() == 2)
            ui->mainTabs->removeTab(1);
    }
    ui->rescaleFrame->setEnabled(index != 5);
    ui->placeableWithTransparencyCheck->setEnabled(index <= 1);
    if (index <= 1 && ui->placeableWithTransparencyCheck->isChecked())
        ui->transparentBitmapKeys->setEnabled(true);
}

void MainWindow::on_snapCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("snap","binary");
        break;
    case 2:
        replaceUserOption("snap","decimal");
        break;
    case 3:
        replaceUserOption("snap","fine");
        break;
    case 0:
    default:
        replaceUserOption("snap","none");
        break;
    }
}

void MainWindow::on_snapTVertsCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("tvert_snap","256");
        break;
    case 2:
        replaceUserOption("tvert_snap","512");
        break;
    case 3:
        replaceUserOption("tvert_snap","1024");
        break;
    case 0:
    default:
        replaceUserOption("tvert_snap","no");
        break;
    }
}

void MainWindow::on_renderShadowsCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("shadow","all");
        break;
    case 2:
        replaceUserOption("shadow","none");
        break;
    case 0:
    default:
        replaceUserOption("shadow","default");
        break;
    }
}

void MainWindow::on_repivotCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("repivot","all");
        break;
    case 2:
        replaceUserOption("repivot","none");
        break;
    case 0:
    default:
        replaceUserOption("repivot","if_needed");
        break;
    }
    ui->repivotBox->setEnabled(index <= 1);
}

void MainWindow::on_allowSplittingCheck_toggled(bool checked)
{
    replaceUserOption("allow_split", checked ? "yes" : "no");
    ui->allowSplittingFrame->setEnabled(checked);
}

void MainWindow::on_subObjectSpin_editingFinished()
{
    auto val = ui->subObjectSpin->cleanText();
    replaceUserOption("min_Size", val);
}

void MainWindow::on_smoothingGroupsCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("use_Smoothed","ignore");
        break;
    case 2:
        replaceUserOption("use_Smoothed","protect");
        break;
    case 0:
    default:
        replaceUserOption("use_Smoothed","use");
        break;
    }
}

void MainWindow::on_splitFirstCombo_currentIndexChanged(int index)
{
    replaceUserOption("split_Priority", index ? "concave" : "convex");
}

void MainWindow::on_pivotsBelowZeroZCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("'pivots_below_z=0'","allow");
        break;
    case 2:
        replaceUserOption("'pivots_below_z=0'","slice");
        break;
    case 0:
    default:
        replaceUserOption("'pivots_below_z=0'","disallow");
        break;
    }
}

void MainWindow::on_moveBadPivotsCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("move_bad_pivots","top");
        break;
    case 2:
        replaceUserOption("move_bad_pivots","middle");
        break;
    case 3:
        replaceUserOption("move_bad_pivots","bottom");
        break;
    case 0:
    default:
        replaceUserOption("move_bad_pivots","no");
        break;
    }
}

void MainWindow::on_forceWhiteCheck_toggled(bool checked)
{
    replaceUserOption("force_white", checked ? "yes" : "no");
}

void MainWindow::on_repairAABBCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("fix_overhangs","no");
        break;
    case 2:
        replaceUserOption("fix_overhangs","interior_only");
        break;
    case 0:
    default:
        replaceUserOption("fix_overhangs","yes");
        break;
    }
}

void MainWindow::on_changeWokMatCheck_toggled(bool checked)
{
    replaceUserOption("map_aabb_material", checked ? "yes" : "no");
    ui->changeWokMatGroupBox->setEnabled(checked);
}

void MainWindow::on_raiseLowerCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("tile_raise","raise");
        break;
    case 2:
        replaceUserOption("tile_raise","lower");
        break;
    case 0:
    default:
        replaceUserOption("tile_raise","no");
        break;
    }
    ui->raiseLowerAmountSpin->setEnabled(index >= 1);
}

void MainWindow::on_raiseLowerAmountSpin_editingFinished()
{
    auto val = ui->raiseLowerAmountSpin->cleanText();
    replaceUserOption("tile_raise_amount", val);
}

void MainWindow::on_sliceForTileFadeCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("slice","no");
        break;
    case 2:
        replaceUserOption("slice","undo");
        break;
    case 0:
    default:
        replaceUserOption("slice","yes");
        break;
    }
    ui->sliceHeightFrame->setEnabled(index == 0);

}

void MainWindow::on_foliageCombo_currentIndexChanged(int index)
{
    switch(index)
    {
        case 1:
            replaceUserOption("foliage","tilefade");
            break;
        case 2:
            replaceUserOption("foliage","animate");
            break;
        case 3:
            replaceUserOption("foliage","de-animate");
            break;
        case 4:
            replaceUserOption("foliage","ignore");
            break;
        case 0:
        default:
            replaceUserOption("foliage","no_change");
            break;
    }
    ui->foliageBitmapKeys->setEnabled(index != 4);
    ui->foliageBitmapKeysLabel->setEnabled(index != 4);

}

void MainWindow::on_groundRotateTextureCombo_currentIndexChanged(int index)
{
    switch(index)
    {
        case 1:
            replaceUserOption("rotate_ground","1");
            break;
        case 2:
            replaceUserOption("rotate_ground","0");
            break;
        case 0:
        default:
            replaceUserOption("rotate_ground","no_change");
            break;
    }
    bool showGroundTextEdit = ui->groundRotateTextureCombo->currentIndex() ||
                              ui->retileGroundPlanesCombo->currentIndex() ||
                              ui->tileEdgeChamfersCombo->currentIndex();
    ui->groundBitmapKeys->setEnabled(showGroundTextEdit);
    ui->groundBitmapKeysLabel->setEnabled(showGroundTextEdit);
}

void MainWindow::on_tileEdgeChamfersCombo_currentIndexChanged(int index)
{
    switch(index)
    {
        case 1:
            replaceUserOption("chamfer","add");
            break;
        case 2:
            replaceUserOption("chamfer","delete");
            break;
        case 0:
        default:
            replaceUserOption("chamfer","no_change");
            break;
    }
    bool showGroundTextEdit = ui->groundRotateTextureCombo->currentIndex() ||
                              ui->retileGroundPlanesCombo->currentIndex() ||
                              ui->tileEdgeChamfersCombo->currentIndex();
    ui->groundBitmapKeys->setEnabled(showGroundTextEdit);
    ui->groundBitmapKeysLabel->setEnabled(showGroundTextEdit);
}

void MainWindow::on_retileGroundPlanesCombo_currentIndexChanged(int index)
{
    switch(index)
    {
        case 1:
            replaceUserOption("tile_ground","1");
            break;
        case 2:
            replaceUserOption("tile_ground","2");
            break;
        case 3:
            replaceUserOption("tile_ground","3");
            break;
        case 0:
        default:
            replaceUserOption("tile_ground","no_change");
            break;
    }
    bool showGroundTextEdit = ui->groundRotateTextureCombo->currentIndex() ||
                              ui->retileGroundPlanesCombo->currentIndex() ||
                              ui->tileEdgeChamfersCombo->currentIndex();
    ui->groundBitmapKeys->setEnabled(showGroundTextEdit);
    ui->groundBitmapKeysLabel->setEnabled(showGroundTextEdit);
}

void MainWindow::on_meshMergeCheck_toggled(bool checked)
{
    replaceUserOption("merge_by_bitmap", checked ? "yes" : "no");
}

void MainWindow::on_placeableWithTransparencyCheck_toggled(bool checked)
{
    if (checked)
    {
        replaceUserOption("placeable_with_transparency","yes");
        if (ui->modelClassCombo->currentIndex() <= 1)
            ui->transBitmapKeyFrame->setEnabled(true);
    }
    else
    {
        replaceUserOption("placeable_with_transparency","no");
        ui->transBitmapKeyFrame->setEnabled(false);
    }
}

void MainWindow::on_animateSplotchesCheck_toggled(bool checked)
{
    if (checked)
    {
        replaceUserOption("splotch","animate");
        ui->splotchBitmapKeysLabel->setEnabled(true);
        ui->splotchBitmapKeys->setEnabled(true);
    }
    else
    {
        replaceUserOption("splotch","ignore");
        ui->splotchBitmapKeysLabel->setEnabled(false);
        ui->splotchBitmapKeys->setEnabled(false);
    }
}

void MainWindow::on_transparentBitmapKeys_editingFinished()
{
    replaceUserOption("transparency_key",ui->transparentBitmapKeys->text().toStdString().c_str());
}

void MainWindow::on_cullInvisibleCheck_toggled(bool checked)
{
    replaceUserOption("invisible_mesh_cull", checked ? "yes" : "no");
}

void MainWindow::on_renderTrimeshCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("render","all");
        break;
    case 2:
        replaceUserOption("render","none");
        break;
    case 0:
    default:
        replaceUserOption("render","default");
        break;
    }
}

void MainWindow::on_changeWokMatFromSpin_editingFinished()
{
    auto val = ui->changeWokMatFromSpin->cleanText();
    replaceUserOption("map_aabb_from", val);
}

void MainWindow::on_changeWokMatToSpin_editingFinished()
{
    auto val = ui->changeWokMatToSpin->cleanText();
    replaceUserOption("map_aabb_to", val);
}

void MainWindow::on_waterFixupsCheck_toggled(bool checked)
{
    ui->waterFrame->setEnabled(checked);
}

void MainWindow::on_waterBitmapKeys_editingFinished()
{
    replaceUserOption("water_key", ui->waterBitmapKeys->text());
}

void MainWindow::on_foliageBitmapKeys_editingFinished()
{
    replaceUserOption("foliage_key", ui->foliageBitmapKeys->text());
}

void MainWindow::on_splotchBitmapKeys_editingFinished()
{
    replaceUserOption("splotch_key", ui->splotchBitmapKeys->text());
}

void MainWindow::on_groundBitmapKeys_editingFinished()
{
    replaceUserOption("ground_key", ui->groundBitmapKeys->text());
}

void MainWindow::on_dynamicWaterCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("dynamic_water","no");
        break;
    case 2:
        replaceUserOption("dynamic_water","wavy");
        break;
    case 0:
    default:
        replaceUserOption("dynamic_water","yes");
        break;
    }
    ui->waveHeightFrame->setEnabled(index == 2);
    ui->retileWaterCombo->setEnabled(index != 2);
    ui->retileWaterLabel->setEnabled(index != 2);
}

void MainWindow::on_waveHeightSpin_editingFinished()
{
    auto val = ui->waveHeightSpin->cleanText();
    replaceUserOption("wave_height", val);

}

void MainWindow::on_waterRotateTextureCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("rotate_water","1");
        break;
    case 2:
        replaceUserOption("rotate_water","0");
        break;
    case 0:
    default:
        replaceUserOption("rotate_water","no_change");
        break;
    }
}

void MainWindow::on_retileWaterCombo_currentIndexChanged(int index)
{
    switch(index)
    {
    case 1:
        replaceUserOption("tile_water","1");
        break;
    case 2:
        replaceUserOption("tile_water","2");
        break;
    case 3:
        replaceUserOption("tile_water","3");
        break;
    case 0:
    default:
        replaceUserOption("tile_water","no_change");
        break;
    }
}

void MainWindow::on_filesTable_customContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = ui->filesTable->mapToGlobal(pos);

    // Create menu and insert some actions
    QMenu myMenu;
    myMenu.addAction(tr("Copy path to clipboard"), this, SLOT(copyToClipboard()));
    myMenu.exec(globalPos);
}

void MainWindow::on_filesTable_doubleClicked(const QModelIndex &index)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
    QString cellText = index.siblingAtColumn(0).data().toString();
    ui->debugTextBrowser->moveCursor(QTextCursor::Start);
    if (!ui->debugTextBrowser->find(cellText))
        ui->debugTextBrowser->moveCursor(QTextCursor::End);
#endif
}

void MainWindow::setRescaleOption()
{
    QString rescaleXYZ = "no";
    if (ui->rescaleXSpin->value() != 1 || ui->rescaleYSpin->value() != 1 || ui->rescaleZSpin->value() != 1)
    {
        rescaleXYZ = "[" % QString::number(ui->rescaleXSpin->value(),'g',4) % "," %
                     QString::number(ui->rescaleYSpin->value(),'g',4) % "," %
                     QString::number(ui->rescaleZSpin->value(),'g',4) % "]";
    }
    replaceUserOption("rescaleXYZ",rescaleXYZ);
}

void MainWindow::on_rescaleLockBtn_clicked(bool checked)
{
    if (checked)
    {
        ui->rescaleLockBtn->setIcon(m_iconLockRescaleBtn);
    }
    else
        ui->rescaleLockBtn->setIcon(m_iconUnlockRescaleBtn);

    ui->rescaleYSpin->setValue(ui->rescaleXSpin->value());
    ui->rescaleZSpin->setValue(ui->rescaleXSpin->value());
    ui->rescaleYSpin->setEnabled(!checked);
    ui->rescaleZSpin->setEnabled(!checked);
}

void MainWindow::on_rescaleXSpin_valueChanged(double arg1)
{
    if (ui->rescaleLockBtn->isChecked())
    {
        QSignalBlocker blockY(ui->rescaleYSpin);
        QSignalBlocker blockZ(ui->rescaleZSpin);
        ui->rescaleYSpin->setValue(arg1);
        ui->rescaleZSpin->setValue(arg1);
    }
    setRescaleOption();
}

void MainWindow::on_rescaleYSpin_valueChanged(double)
{
    setRescaleOption();
}

void MainWindow::on_rescaleZSpin_valueChanged(double)
{
    setRescaleOption();
}
