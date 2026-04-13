#include "fsmodel.h"
#include "mainwindow.h"
#include "modelviewport.h"
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
#include <QSplitter>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QSurfaceFormat>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QWhatsThis>
#include <QWindow>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->debugTextBrowser->insertHtml(tr("Welcome to Clean Models:EE QT!<br>"));

#ifdef Q_OS_WIN
    m_sBinaryName = "cleanmodels.exe";
#else
    m_sBinaryName = "cleanmodels";
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
        QString foundMsg = "Clean Models CLI found at " % m_sBinaryPath;
        ui->debugTextBrowser->insertHtml(tr(foundMsg.toStdString().c_str()));
        auto sb = ui->debugTextBrowser->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
    else
    {
        QString errorMsg = "Could not find the " % m_sBinaryName % " executable in the current directory or in your path!";
        QMessageBox::critical(nullptr, "No cleanmodels CLI", tr(errorMsg.toStdString().c_str()));
        ui->debugTextBrowser->insertHtml(tr(errorMsg.toStdString().c_str()));
        auto sb = ui->debugTextBrowser->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

    m_pCleanProcess = new QProcess(this);
    m_bCleanRunning = false;

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

    // 3D viewport: replace the debugTextBrowser in the splitter with
    // a horizontal splitter containing [debugTextBrowser | viewport]
    m_viewport = new ModelViewport(this);
    m_viewport->setCliBinaryPath(m_sBinaryPath);
    m_viewport->setMinimumSize(200, 150);

    auto *hSplitter = new QSplitter(Qt::Horizontal, this);
    QWidget *debugParent = ui->debugTextBrowser->parentWidget();
    QSplitter *parentSplitter = qobject_cast<QSplitter *>(debugParent);
    if (parentSplitter)
    {
        int idx = parentSplitter->indexOf(ui->debugTextBrowser);
        hSplitter->addWidget(ui->debugTextBrowser);
        hSplitter->addWidget(m_viewport);
        hSplitter->setSizes({400, 400});
        parentSplitter->insertWidget(idx, hSplitter);
    }
    else
    {
        hSplitter->addWidget(ui->debugTextBrowser);
        hSplitter->addWidget(m_viewport);
        hSplitter->setSizes({400, 400});
    }

    connect(m_viewport, &ModelViewport::previewError, this, [this](const QString &msg) {
        appendDebugHtml("<p><span style=\"color:orange;\">Preview: " + msg.toHtmlEscaped() + "</span></p><br>");
    });

    loadSettings();

    connect(m_pCleanProcess, &QProcess::finished, this, &MainWindow::onCleanFinished);
    connect(ui->actionHelp, &QAction::triggered, this, &MainWindow::onHelpTriggered);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAboutTriggered);
    connect(ui->actionSavePreset, &QAction::triggered, this, &MainWindow::onSaveConfigTriggered);
    connect(ui->actionLoadPreset, &QAction::triggered, this, &MainWindow::onLoadConfigTriggered);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::onQuitTriggered);
    connect(&m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onDirectoryContentsChanged);

    readSettings();
}

MainWindow::~MainWindow()
{
    delete ui;
    m_pCleanProcess->close();
    delete m_pCleanProcess;
}

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

void MainWindow::loadSettings()
{
    QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    s.beginGroup("options");

    QString inDir = s.value("indir").toString();
    if (!inDir.isEmpty())
    {
        onUpdateInDir(inDir);
        QDir absDir;
        m_pFileSystemModel->setRootPath(absDir.absoluteFilePath(inDir));
    }

    QString outDir = s.value("outdir").toString();
    if (!outDir.isEmpty())
    {
        m_sOutDir = outDir;
        ui->outDirectory->setText(m_sOutDir);
        QDir absDir;
        m_pFileSystemModel->setRootPath(absDir.absoluteFilePath(m_sOutDir));
    }

    ui->filePattern->setText(s.value("pattern", "*.mdl").toString());
    ui->logFileName->setText(s.value("logfile").toString());
    ui->summaryLogFileName->setText(s.value("summary_log").toString());
    ui->modelClassCombo->setCurrentIndex(s.value("classification", 0).toInt());
    ui->snapCombo->setCurrentIndex(s.value("snap", 0).toInt());
    ui->snapTVertsCombo->setCurrentIndex(s.value("tvert_snap", 0).toInt());
    ui->smoothingGroupsCombo->setCurrentIndex(s.value("smoothing_groups", 0).toInt());
    ui->repairAABBCombo->setCurrentIndex(s.value("fix_overhangs", 0).toInt());
    ui->dynamicWaterCombo->setCurrentIndex(s.value("dynamic_water", 0).toInt());
    ui->waterRotateTextureCombo->setCurrentIndex(s.value("rotate_water", 0).toInt());
    ui->retileWaterCombo->setCurrentIndex(s.value("tile_water", 0).toInt());
    ui->raiseLowerCombo->setCurrentIndex(s.value("tile_raise", 0).toInt());
    ui->raiseLowerAmountSpin->setValue(s.value("tile_raise_amount", 0.0).toDouble());
    ui->sliceForTileFadeCombo->setCurrentIndex(s.value("slice", 0).toInt());
    ui->renderTrimeshCombo->setCurrentIndex(s.value("render", 0).toInt());
    ui->renderShadowsCombo->setCurrentIndex(s.value("shadow", 0).toInt());
    ui->repivotCombo->setCurrentIndex(s.value("repivot", 0).toInt());
    ui->pivotsBelowZeroZCombo->setCurrentIndex(s.value("pivots_below_z0", 0).toInt());
    ui->moveBadPivotsCombo->setCurrentIndex(s.value("move_bad_pivots", 0).toInt());
    ui->foliageCombo->setCurrentIndex(s.value("foliage", 0).toInt());
    ui->groundRotateTextureCombo->setCurrentIndex(s.value("rotate_ground", 0).toInt());
    ui->tileEdgeChamfersCombo->setCurrentIndex(s.value("chamfer", 0).toInt());
    ui->retileGroundPlanesCombo->setCurrentIndex(s.value("tile_ground", 0).toInt());
    ui->cullInvisibleCheck->setChecked(s.value("invisible_mesh_cull", false).toBool());
    ui->changeWokMatCheck->setChecked(s.value("map_aabb_material", false).toBool());
    ui->changeWokMatGroupBox->setEnabled(s.value("map_aabb_material", false).toBool());
    ui->allowSplittingCheck->setChecked(s.value("allow_split", false).toBool());
    ui->waterFixupsCheck->setChecked(s.value("do_water", false).toBool());
    ui->waterFrame->setEnabled(s.value("do_water", false).toBool());
    ui->waterBitmapKeys->setText(s.value("water_key").toString());
    ui->groundBitmapKeys->setText(s.value("ground_key").toString());
    ui->splotchBitmapKeys->setText(s.value("splotch_key").toString());
    ui->foliageBitmapKeys->setText(s.value("foliage_key").toString());
    ui->subObjectSpin->setValue(s.value("min_size", 0).toInt());
    ui->meshMergeCheck->setChecked(s.value("merge_by_bitmap", false).toBool());
    ui->placeableWithTransparencyCheck->setChecked(s.value("placeable_with_transparency", false).toBool());
    ui->animateSplotchesCheck->setChecked(s.value("animate_splotches", false).toBool());
    ui->forceWhiteCheck->setChecked(s.value("force_white", false).toBool());
    ui->transparentBitmapKeys->setText(s.value("transparency_key").toString());
    ui->waveHeightSpin->setValue(s.value("wave_height", 0.0).toDouble());
    ui->changeWokMatFromSpin->setValue(s.value("map_aabb_from", 0).toInt());
    ui->changeWokMatToSpin->setValue(s.value("map_aabb_to", 0).toInt());
    ui->rescaleXSpin->setValue(s.value("rescale_x", 1.0).toDouble());
    ui->rescaleYSpin->setValue(s.value("rescale_y", 1.0).toDouble());
    ui->rescaleZSpin->setValue(s.value("rescale_z", 1.0).toDouble());

    s.endGroup();
}

void MainWindow::saveSettings()
{
    QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    s.beginGroup("options");

    s.setValue("indir", m_sInDir);
    s.setValue("outdir", m_sOutDir);
    s.setValue("pattern", ui->filePattern->text());
    s.setValue("logfile", ui->logFileName->text());
    s.setValue("summary_log", ui->summaryLogFileName->text());
    s.setValue("classification", ui->modelClassCombo->currentIndex());
    s.setValue("snap", ui->snapCombo->currentIndex());
    s.setValue("tvert_snap", ui->snapTVertsCombo->currentIndex());
    s.setValue("smoothing_groups", ui->smoothingGroupsCombo->currentIndex());
    s.setValue("fix_overhangs", ui->repairAABBCombo->currentIndex());
    s.setValue("dynamic_water", ui->dynamicWaterCombo->currentIndex());
    s.setValue("rotate_water", ui->waterRotateTextureCombo->currentIndex());
    s.setValue("tile_water", ui->retileWaterCombo->currentIndex());
    s.setValue("tile_raise", ui->raiseLowerCombo->currentIndex());
    s.setValue("tile_raise_amount", ui->raiseLowerAmountSpin->value());
    s.setValue("slice", ui->sliceForTileFadeCombo->currentIndex());
    s.setValue("render", ui->renderTrimeshCombo->currentIndex());
    s.setValue("shadow", ui->renderShadowsCombo->currentIndex());
    s.setValue("repivot", ui->repivotCombo->currentIndex());
    s.setValue("pivots_below_z0", ui->pivotsBelowZeroZCombo->currentIndex());
    s.setValue("move_bad_pivots", ui->moveBadPivotsCombo->currentIndex());
    s.setValue("foliage", ui->foliageCombo->currentIndex());
    s.setValue("rotate_ground", ui->groundRotateTextureCombo->currentIndex());
    s.setValue("chamfer", ui->tileEdgeChamfersCombo->currentIndex());
    s.setValue("tile_ground", ui->retileGroundPlanesCombo->currentIndex());
    s.setValue("invisible_mesh_cull", ui->cullInvisibleCheck->isChecked());
    s.setValue("map_aabb_material", ui->changeWokMatCheck->isChecked());
    s.setValue("allow_split", ui->allowSplittingCheck->isChecked());
    s.setValue("do_water", ui->waterFixupsCheck->isChecked());
    s.setValue("water_key", ui->waterBitmapKeys->text());
    s.setValue("ground_key", ui->groundBitmapKeys->text());
    s.setValue("splotch_key", ui->splotchBitmapKeys->text());
    s.setValue("foliage_key", ui->foliageBitmapKeys->text());
    s.setValue("min_size", ui->subObjectSpin->value());
    s.setValue("merge_by_bitmap", ui->meshMergeCheck->isChecked());
    s.setValue("placeable_with_transparency", ui->placeableWithTransparencyCheck->isChecked());
    s.setValue("animate_splotches", ui->animateSplotchesCheck->isChecked());
    s.setValue("force_white", ui->forceWhiteCheck->isChecked());
    s.setValue("transparency_key", ui->transparentBitmapKeys->text());
    s.setValue("wave_height", ui->waveHeightSpin->value());
    s.setValue("map_aabb_from", ui->changeWokMatFromSpin->value());
    s.setValue("map_aabb_to", ui->changeWokMatToSpin->value());
    s.setValue("rescale_x", ui->rescaleXSpin->value());
    s.setValue("rescale_y", ui->rescaleYSpin->value());
    s.setValue("rescale_z", ui->rescaleZSpin->value());

    s.endGroup();
}

void MainWindow::closeEvent(QCloseEvent*)
{
    writeSettings();
    saveSettings();
}

void MainWindow::onLoadConfigTriggered()
{
    QFileDialog fileDialog;
    fileDialog.setAcceptMode(QFileDialog::AcceptMode::AcceptOpen);
    QStringList nameFilters;
    nameFilters.append("Clean Models Config (*.ini)");
    nameFilters.append("Legacy Config (*.cm *.pl)");
    fileDialog.setNameFilters(nameFilters);
    fileDialog.setDirectory(QDir::currentPath());
    if (fileDialog.exec())
    {
        QString fileName = fileDialog.selectedFiles()[0];
        QSettings imported(fileName, QSettings::IniFormat);
        QSettings current(QCoreApplication::organizationName(), QCoreApplication::applicationName());
        for (const auto &key : imported.allKeys())
            current.setValue(key, imported.value(key));
        loadSettings();
    }
}

void MainWindow::onSaveConfigTriggered()
{
    saveSettings();

    QFileDialog fileDialog;
    fileDialog.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);
    fileDialog.setFileMode(QFileDialog::AnyFile);
    fileDialog.setDefaultSuffix("ini");
    QStringList nameFilters;
    nameFilters.append("Clean Models Config (*.ini)");
    fileDialog.setNameFilters(nameFilters);
    fileDialog.setDirectory(QDir::currentPath());
    if (fileDialog.exec())
    {
        QString fileName = fileDialog.selectedFiles()[0];
        QSettings current(QCoreApplication::organizationName(), QCoreApplication::applicationName());
        QSettings exported(fileName, QSettings::IniFormat);
        for (const auto &key : current.allKeys())
            exported.setValue(key, current.value(key));
    }
}

void MainWindow::onAboutTriggered()
{
    QMessageBox::about(this, tr("About Clean Models:EE QT"),
                       tr("A front end to Clean Models, a utility to tidy up 3d models\n"
                          "for usage in Neverwinter Nights: Enhanced Edition.\n\n"
                          "Powered by cleanmodels (Go CLI)."));
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
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable | QDir::CaseSensitive);
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
        if (!inputFile.open(QIODevice::ReadOnly))
            continue;
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
            ui->inDirectory->setStyleSheet("");
        }
        else
            ui->inDirectory->setStyleSheet("color: #FF0000");
    }
    else
    {
        ui->inDirectory->setStatusTip(tr("Input folder resolved as ") %f);
        ui->inDirectory->setStyleSheet("");
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
        ui->inDirectory->setStyleSheet("");
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
    ui->outDirectory->setStatusTip(tr("Output folder resolved as ") % f);
}

void MainWindow::on_filePattern_textChanged(const QString &)
{
    m_dirWatcherTimer->stop();
    m_bFilesHaveChanged = false;
    updateFileListing();
    m_dirWatcherTimer->start();
}

void MainWindow::on_modelClassCombo_currentIndexChanged(int index)
{
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

void MainWindow::on_snapCombo_currentIndexChanged(int) {}
void MainWindow::on_snapTVertsCombo_currentIndexChanged(int) {}
void MainWindow::on_renderShadowsCombo_currentIndexChanged(int) {}

void MainWindow::on_repivotCombo_currentIndexChanged(int index)
{
    ui->repivotBox->setEnabled(index <= 1);
}

void MainWindow::on_allowSplittingCheck_toggled(bool checked)
{
    ui->allowSplittingFrame->setEnabled(checked);
}

void MainWindow::on_subObjectSpin_editingFinished() {}
void MainWindow::on_smoothingGroupsCombo_currentIndexChanged(int) {}
void MainWindow::on_splitFirstCombo_currentIndexChanged(int) {}

void MainWindow::on_pivotsBelowZeroZCombo_currentIndexChanged(int) {}
void MainWindow::on_moveBadPivotsCombo_currentIndexChanged(int) {}
void MainWindow::on_forceWhiteCheck_toggled(bool) {}

void MainWindow::on_repairAABBCombo_currentIndexChanged(int) {}

void MainWindow::on_changeWokMatCheck_toggled(bool checked)
{
    ui->changeWokMatGroupBox->setEnabled(checked);
}

void MainWindow::on_raiseLowerCombo_currentIndexChanged(int index)
{
    ui->raiseLowerAmountSpin->setEnabled(index >= 1);
}

void MainWindow::on_raiseLowerAmountSpin_editingFinished() {}

void MainWindow::on_sliceForTileFadeCombo_currentIndexChanged(int index)
{
    ui->sliceHeightFrame->setEnabled(index == 0);
}

void MainWindow::on_foliageCombo_currentIndexChanged(int index)
{
    ui->foliageBitmapKeys->setEnabled(index != 4);
    ui->foliageBitmapKeysLabel->setEnabled(index != 4);
}

void MainWindow::on_groundRotateTextureCombo_currentIndexChanged(int)
{
    bool showGroundTextEdit = ui->groundRotateTextureCombo->currentIndex() ||
                              ui->retileGroundPlanesCombo->currentIndex() ||
                              ui->tileEdgeChamfersCombo->currentIndex();
    ui->groundBitmapKeys->setEnabled(showGroundTextEdit);
    ui->groundBitmapKeysLabel->setEnabled(showGroundTextEdit);
}

void MainWindow::on_tileEdgeChamfersCombo_currentIndexChanged(int)
{
    bool showGroundTextEdit = ui->groundRotateTextureCombo->currentIndex() ||
                              ui->retileGroundPlanesCombo->currentIndex() ||
                              ui->tileEdgeChamfersCombo->currentIndex();
    ui->groundBitmapKeys->setEnabled(showGroundTextEdit);
    ui->groundBitmapKeysLabel->setEnabled(showGroundTextEdit);
}

void MainWindow::on_retileGroundPlanesCombo_currentIndexChanged(int)
{
    bool showGroundTextEdit = ui->groundRotateTextureCombo->currentIndex() ||
                              ui->retileGroundPlanesCombo->currentIndex() ||
                              ui->tileEdgeChamfersCombo->currentIndex();
    ui->groundBitmapKeys->setEnabled(showGroundTextEdit);
    ui->groundBitmapKeysLabel->setEnabled(showGroundTextEdit);
}

void MainWindow::on_meshMergeCheck_toggled(bool) {}

void MainWindow::on_placeableWithTransparencyCheck_toggled(bool checked)
{
    if (checked && ui->modelClassCombo->currentIndex() <= 1)
        ui->transBitmapKeyFrame->setEnabled(true);
    else
        ui->transBitmapKeyFrame->setEnabled(false);
}

void MainWindow::on_animateSplotchesCheck_toggled(bool checked)
{
    ui->splotchBitmapKeysLabel->setEnabled(checked);
    ui->splotchBitmapKeys->setEnabled(checked);
}

void MainWindow::on_transparentBitmapKeys_editingFinished() {}
void MainWindow::on_cullInvisibleCheck_toggled(bool) {}
void MainWindow::on_renderTrimeshCombo_currentIndexChanged(int) {}
void MainWindow::on_changeWokMatFromSpin_editingFinished() {}
void MainWindow::on_changeWokMatToSpin_editingFinished() {}

void MainWindow::on_waterFixupsCheck_toggled(bool checked)
{
    ui->waterFrame->setEnabled(checked);
}

void MainWindow::on_waterBitmapKeys_editingFinished() {}
void MainWindow::on_foliageBitmapKeys_editingFinished() {}
void MainWindow::on_splotchBitmapKeys_editingFinished() {}
void MainWindow::on_groundBitmapKeys_editingFinished() {}

void MainWindow::on_dynamicWaterCombo_currentIndexChanged(int index)
{
    ui->waveHeightFrame->setEnabled(index == 2);
    ui->retileWaterCombo->setEnabled(index != 2);
    ui->retileWaterLabel->setEnabled(index != 2);
}

void MainWindow::on_waveHeightSpin_editingFinished() {}
void MainWindow::on_waterRotateTextureCombo_currentIndexChanged(int) {}
void MainWindow::on_retileWaterCombo_currentIndexChanged(int) {}

void MainWindow::on_filesTable_customContextMenuRequested(const QPoint &pos)
{
    QPoint globalPos = ui->filesTable->mapToGlobal(pos);
    QMenu myMenu;
    myMenu.addAction(tr("Copy path to clipboard"), this, SLOT(copyToClipboard()));
    myMenu.exec(globalPos);
}

void MainWindow::on_filesTable_doubleClicked(const QModelIndex &index)
{
    QString cellText = index.siblingAtColumn(0).data().toString();
    ui->debugTextBrowser->moveCursor(QTextCursor::Start);
    if (!ui->debugTextBrowser->find(cellText))
        ui->debugTextBrowser->moveCursor(QTextCursor::End);

    if (m_viewport && !cellText.isEmpty() && !m_sInDir.isEmpty())
    {
        QString fullPath = m_sInDir + "/" + cellText;
        m_viewport->previewFile(fullPath);
    }
}

void MainWindow::setRescaleOption()
{
    // Settings are saved at close via saveSettings()
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
