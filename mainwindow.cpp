#include "constants.h"
#include "fsmodel.h"
#include "loghtml.h"
#include "mainwindow.h"
#include "metrics.h"
#include "modelviewport.h"
#include "ui_mainwindow.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QInputDialog>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QMimeData>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QPalette>
#include <QMessageBox>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWhatsThis>
#include <QWindow>

// ---------------------------------------------------------------------------
// Collapsible section: flat button with disclosure triangle + content widget
// ---------------------------------------------------------------------------
QWidget *MainWindow::createCollapsibleGroup(const QString &title, QWidget **contentOut, bool startCollapsed)
{
    auto *container = new QWidget;
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    auto *header = new QPushButton(container);
    header->setFlat(true);
    header->setStyleSheet("QPushButton { text-align: left; padding: 4px 0; font-weight: bold; }"
                          "QPushButton:flat { border: none; }");

    auto *content = new QWidget(container);
    content->setVisible(!startCollapsed);

    auto updateArrow = [header, title](bool expanded) {
        header->setText(QString(expanded ? "\u25BC " : "\u25B6 ") + title);
    };
    updateArrow(!startCollapsed);

    connect(header, &QPushButton::clicked, content, [content, header, title, updateArrow] {
        bool willExpand = !content->isVisible();
        content->setVisible(willExpand);
        updateArrow(willExpand);
    });

    vbox->addWidget(header);
    vbox->addWidget(content);

    *contentOut = content;
    return container;
}

// ---------------------------------------------------------------------------
// Build the entire UI programmatically
// ---------------------------------------------------------------------------
static void fillCombo(QComboBox *combo, const ComboOption *opts, int count)
{
    for (int i = 0; i < count; ++i)
        combo->addItem(opts[i].label, QString(opts[i].cliValue));
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

template<size_t N>
static void fillCombo(QComboBox *combo, const ComboOption (&opts)[N])
{
    fillCombo(combo, opts, static_cast<int>(N));
}

static QLabel *boldLabel(const QString &text)
{
    auto *lbl = new QLabel(text);
    QFont f = lbl->font();
    f.setBold(true);
    lbl->setFont(f);
    return lbl;
}

static QLabel *smallLabel(const QString &text)
{
    auto *lbl = new QLabel(text);
    QPalette p = lbl->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::WindowText).darker(130));
    lbl->setPalette(p);
    return lbl;
}

QFormLayout *MainWindow::makeStandardForm(int spacing)
{
    auto *form = new QFormLayout;
    form->setSpacing(spacing);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setLabelAlignment(Qt::AlignLeft);
    return form;
}

void MainWindow::updateModeUI()
{
    bool cleanMode = m_radioClean->isChecked();
    if (m_radioDecompile->isChecked()) {
        m_cleanButton->setText("Decompile");
        m_cleanButton->setIcon(m_iconDecompileButton);
    } else if (m_radioCompile->isChecked()) {
        m_cleanButton->setText("Compile");
        m_cleanButton->setIcon(m_iconCleanButton);
    } else {
        m_cleanButton->setText("Clean");
        m_cleanButton->setIcon(m_iconCleanButton);
    }
    m_allFixesCheck->setEnabled(cleanMode);
    m_fixesDetailWidget->setEnabled(cleanMode);
    m_advancedGroup->setEnabled(cleanMode);
    m_tileGroup->setEnabled(cleanMode);
    m_pivotGroup->setEnabled(cleanMode);
}

static QFrame *makeHLine()
{
    auto *f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

void MainWindow::buildUi()
{
    auto *central = ui->centralWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(Layout::RootMargin, Layout::RootMargin, Layout::RootMargin, Layout::RootMargin);
    root->setSpacing(0);

    // ====================================================================
    //  LEFT SIDEBAR — options panel
    // ====================================================================
    m_sidebarWidget = new QWidget;
    m_sidebarWidget->setMinimumWidth(Layout::SidebarMinWidth);
    m_sidebarWidget->setMaximumWidth(Layout::SidebarMaxWidth);
    auto *sidebarLayout = new QVBoxLayout(m_sidebarWidget);
    sidebarLayout->setContentsMargins(Layout::RootMargin, Layout::SidebarTopPad, Layout::RootMargin, Layout::RootMargin);
    sidebarLayout->setSpacing(0);

    // ── I/O paths — all 4 rows in one QFormLayout for uniform spacing ─
    auto *ioForm = makeStandardForm(Layout::DefaultSpacing);
    ioForm->setContentsMargins(0, 0, 0, 0);

    m_indirButton = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), "");
    m_indirButton->setAccessibleName(tr("Browse for input directory"));
    m_indirButton->setToolTip(tr("Browse for input directory"));
    m_outdirButton = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), "");
    m_outdirButton->setAccessibleName(tr("Browse for output directory"));
    m_outdirButton->setToolTip(tr("Browse for output directory"));
    m_inDirectory = new QLineEdit;
    m_inDirectory->setAccessibleName(tr("Input directory"));
    m_inDirectory->setToolTip("Directory containing MDL files to process");
    m_outDirectory = new QLineEdit;
    m_outDirectory->setAccessibleName(tr("Output directory"));
    m_outDirectory->setToolTip("Output directory for processed files (leave empty to overwrite originals)");
    m_filePattern = new QLineEdit("*.mdl");
    m_filePattern->setAccessibleName(tr("File pattern"));
    m_filePattern->setToolTip("Glob pattern to filter files (e.g. *.mdl)");
    m_classificationCombo = new QComboBox;
    m_classificationCombo->setAccessibleName(tr("Model classification"));
    fillCombo(m_classificationCombo, Options::Classification);
    m_classificationCombo->setToolTip("Override model classification (Automatic detects from file)");

    m_inDirectory->setPlaceholderText("Input directory...");
    m_outDirectory->setPlaceholderText("Output (optional)");

    auto *inRow = new QHBoxLayout;
    inRow->setSpacing(Layout::CompactSpacing);
    inRow->addWidget(m_inDirectory, 1);
    inRow->addWidget(m_indirButton);
    ioForm->addRow(boldLabel("In:"), inRow);

    auto *outRow = new QHBoxLayout;
    outRow->setSpacing(Layout::CompactSpacing);
    outRow->addWidget(m_outDirectory, 1);
    outRow->addWidget(m_outdirButton);
    ioForm->addRow(boldLabel("Out:"), outRow);

    ioForm->addRow(boldLabel("Pattern:"), m_filePattern);
    ioForm->addRow(boldLabel("Class:"), m_classificationCombo);

    sidebarLayout->addLayout(ioForm);
    sidebarLayout->addSpacing(Layout::SectionGap);

    // ── Scroll area for options ───────────────────────────────────────
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *optionsWidget = new QWidget;
    auto *optionsLayout = new QVBoxLayout(optionsWidget);
    // Right padding prevents content clipping when vertical scrollbar appears
    optionsLayout->setContentsMargins(0, 0, Layout::SectionGap, 0);
    optionsLayout->setSpacing(Layout::DefaultSpacing);
    scroll->setWidget(optionsWidget);

    // ── Mode ───────────────────────────────────────────────────────────
    auto *modeRow = new QHBoxLayout;
    m_radioClean = new QRadioButton("Clean");
    m_radioClean->setToolTip("Parse, validate, repair, and recompile models");
    m_radioDecompile = new QRadioButton("Decompile");
    m_radioDecompile->setToolTip("Decompile binary MDL to ASCII without cleaning");
    m_radioCompile = new QRadioButton("Compile");
    m_radioCompile->setToolTip("Compile ASCII MDL to binary format");
    m_radioClean->setChecked(true);
    modeRow->addWidget(boldLabel("Mode:"));
    modeRow->addWidget(m_radioClean);
    modeRow->addWidget(m_radioDecompile);
    modeRow->addWidget(m_radioCompile);
    modeRow->addStretch();
    optionsLayout->addLayout(modeRow);

    // ── All Fixes master checkbox ──────────────────────────────────────
    m_allFixesCheck = new QCheckBox("All Fixes (recommended)");
    m_allFixesCheck->setToolTip("Enable all recommended fixes with default settings");
    m_allFixesCheck->setChecked(true);
    optionsLayout->addWidget(m_allFixesCheck);

    m_fixesDetailWidget = new QWidget;
    auto *fixesGrid = new QVBoxLayout(m_fixesDetailWidget);
    fixesGrid->setContentsMargins(Layout::IndentLeft, 0, 0, 0);
    fixesGrid->setSpacing(Layout::DefaultSpacing);

    m_checkValidateAll = new QCheckBox("Run validation checks");
    m_checkValidateAll->setToolTip("Validate model structure, geometry, parameters, animations, and emitters.\n"
                                    "Auto-fixes safe issues like duplicate names and orphaned nodes.");
    m_checkValidateAll->setChecked(true);
    fixesGrid->addWidget(m_checkValidateAll);

    m_checksDetailWidget = new QWidget;
    auto *checksDetailLayout = new QVBoxLayout(m_checksDetailWidget);
    checksDetailLayout->setContentsMargins(Layout::IndentLeft, 0, 0, 0);
    checksDetailLayout->setSpacing(Layout::CompactSpacing);
    m_checksDetailWidget->setVisible(false);
    fixesGrid->addWidget(m_checksDetailWidget);

    connect(m_checkValidateAll, &QCheckBox::toggled, this, [this](bool allOn) {
        m_checksDetailWidget->setVisible(!allOn);
    });

    m_checkStripDegen     = new QCheckBox("Strip degenerate faces");
    m_checkStripDegen->setToolTip("Remove triangles with zero area (collapsed vertices)");
    m_checkFixAnims       = new QCheckBox("Fix animations");
    m_checkFixAnims->setToolTip("Fix animation lengths and missing end-keys");
    m_checkRepairPivots   = new QCheckBox("Repair pivots");
    m_checkRepairPivots->setToolTip("Repair walkmesh pivot points for proper tile pathfinding");
    m_checkFixTilefade    = new QCheckBox("Fix tilefade (TILE only)");
    m_checkFixTilefade->setToolTip("Slice geometry at fade height for tile transparency");
    m_checkTilefadeUndo   = new QCheckBox("Undo tilefade splits");
    m_checkTilefadeUndo->setToolTip("Remove existing tilefade splits from tile geometry");
    m_checkTilefadeUndo->setChecked(false);
    m_checkRebuildAABB    = new QCheckBox("Rebuild AABB");
    m_checkRebuildAABB->setToolTip("Rebuild axis-aligned bounding box tree for walkmesh");
    m_checkReparentChildren = new QCheckBox("Reparent children");
    m_checkReparentChildren->setToolTip("Move child nodes off restricted parent types (AABB, light)");
    m_checkWrapRoot       = new QCheckBox("Wrap root in dummy");
    m_checkWrapRoot->setToolTip("Wrap root node in a dummy if it has geometry");
    m_checkSplitMultiEdge = new QCheckBox("Split multi-edge shadows");
    m_checkSplitMultiEdge->setToolTip("Fix non-manifold edges that break shadow rendering");

    for (auto *cb : {m_checkStripDegen, m_checkFixAnims,
         m_checkRepairPivots, m_checkFixTilefade, m_checkRebuildAABB,
         m_checkReparentChildren, m_checkWrapRoot, m_checkSplitMultiEdge})
    {
        cb->setChecked(true);
        fixesGrid->addWidget(cb);
    }
    fixesGrid->addWidget(m_checkTilefadeUndo);

    m_fixesDetailWidget->setVisible(false);
    optionsLayout->addWidget(m_fixesDetailWidget);

    connect(m_allFixesCheck, &QCheckBox::toggled, this, [this](bool allOn) {
        m_fixesDetailWidget->setVisible(!allOn);
        if (allOn)
        {
            m_checkValidateAll->setChecked(true);
            for (auto *cb : {m_checkStripDegen, m_checkFixAnims,
                 m_checkRepairPivots, m_checkFixTilefade, m_checkRebuildAABB,
                 m_checkReparentChildren, m_checkWrapRoot, m_checkSplitMultiEdge})
                cb->setChecked(true);
        }
    });

    // ── Advanced Options (collapsible) ─────────────────────────────────
    QWidget *advContent;
    m_advancedGroup = createCollapsibleGroup("Advanced Options", &advContent, true);
    auto *advLayout = new QVBoxLayout;
    advLayout->setContentsMargins(Layout::GroupMarginH, Layout::GroupMarginTop, Layout::GroupMarginH, Layout::GroupMarginBottom);
    advLayout->setSpacing(Layout::DefaultSpacing);

    m_scaleXSpin = new QDoubleSpinBox; m_scaleXSpin->setRange(0.01, 100); m_scaleXSpin->setValue(1.0); m_scaleXSpin->setSingleStep(0.1); m_scaleXSpin->setDecimals(2);
    m_scaleXSpin->setToolTip("X-axis scale factor");
    m_scaleYSpin = new QDoubleSpinBox; m_scaleYSpin->setRange(0.01, 100); m_scaleYSpin->setValue(1.0); m_scaleYSpin->setSingleStep(0.1); m_scaleYSpin->setDecimals(2);
    m_scaleYSpin->setToolTip("Y-axis scale factor");
    m_scaleZSpin = new QDoubleSpinBox; m_scaleZSpin->setRange(0.01, 100); m_scaleZSpin->setValue(1.0); m_scaleZSpin->setSingleStep(0.1); m_scaleZSpin->setDecimals(2);
    m_scaleZSpin->setToolTip("Z-axis scale factor");
    m_scaleLockBtn = new QPushButton(QString::fromUtf8("🔒"));
    m_scaleLockBtn->setCheckable(true);
    m_scaleLockBtn->setChecked(true);
    m_scaleLockBtn->setFlat(true);
    m_scaleLockBtn->setAccessibleName(tr("Lock uniform scaling"));
    m_scaleLockBtn->setToolTip(tr("Lock/unlock uniform scaling"));
    m_scaleYSpin->setEnabled(false);
    m_scaleZSpin->setEnabled(false);

    auto *scaleGrid = new QGridLayout;
    scaleGrid->setSpacing(Layout::CompactSpacing);
    scaleGrid->addWidget(new QLabel("Scale X:"), 0, 0);
    scaleGrid->addWidget(m_scaleXSpin, 0, 1);
    scaleGrid->addWidget(m_scaleLockBtn, 0, 2);
    scaleGrid->addWidget(new QLabel("Y:"), 1, 0);
    scaleGrid->addWidget(m_scaleYSpin, 1, 1);
    scaleGrid->addWidget(new QLabel("Z:"), 2, 0);
    scaleGrid->addWidget(m_scaleZSpin, 2, 1);
    scaleGrid->setColumnStretch(1, 1);
    advLayout->addLayout(scaleGrid);

    advLayout->addWidget(makeHLine());

    connect(m_scaleLockBtn, &QPushButton::toggled, this, [this](bool locked) {
        m_scaleLockBtn->setText(locked ? QString::fromUtf8("🔒") : QString::fromUtf8("🔓"));
        m_scaleYSpin->setEnabled(!locked);
        m_scaleZSpin->setEnabled(!locked);
        if (locked) { m_scaleYSpin->setValue(m_scaleXSpin->value()); m_scaleZSpin->setValue(m_scaleXSpin->value()); }
    });
    connect(m_scaleXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (m_scaleLockBtn->isChecked()) {
            QSignalBlocker b1(m_scaleYSpin), b2(m_scaleZSpin);
            m_scaleYSpin->setValue(v); m_scaleZSpin->setValue(v);
        }
    });

    auto *meshForm = makeStandardForm();
    m_snapCombo = new QComboBox; fillCombo(m_snapCombo, Options::Snap);
    m_snapCombo->setToolTip("Snap vertex positions to a grid (reduces file size)");
    m_tvertSnapCombo = new QComboBox; fillCombo(m_tvertSnapCombo, Options::TvertSnap);
    m_tvertSnapCombo->setToolTip("Snap texture coordinates to a grid resolution");
    m_renderCombo = new QComboBox; fillCombo(m_renderCombo, Options::RenderOverride);
    m_renderCombo->setToolTip("Override render flag on all mesh nodes");
    m_shadowCombo = new QComboBox; fillCombo(m_shadowCombo, Options::ShadowOverride);
    m_shadowCombo->setToolTip("Override shadow flag on all mesh nodes");
    meshForm->addRow("Snap:", m_snapCombo);
    meshForm->addRow("TVert Snap:", m_tvertSnapCombo);
    meshForm->addRow("Render:", m_renderCombo);
    meshForm->addRow("Shadow:", m_shadowCombo);
    advLayout->addLayout(meshForm);

    advLayout->addWidget(makeHLine());

    m_forceWhiteCheck = new QCheckBox("Force white ambient/diffuse");
    m_forceWhiteCheck->setToolTip("Set ambient and diffuse colors to white on all meshes");
    m_mergeByBitmapCheck = new QCheckBox("Merge meshes by bitmap");
    m_mergeByBitmapCheck->setToolTip("Merge mesh nodes that share the same texture");
    m_cullInvisibleCheck = new QCheckBox("Cull invisible meshes");
    m_cullInvisibleCheck->setToolTip("Remove mesh nodes with render=0 and no animations");
    m_standardizeTexture0Check = new QCheckBox("Standardize bitmap → texture0");
    m_standardizeTexture0Check->setToolTip("Emit texture0 instead of bitmap in output (EE standard)");
    m_stripEEExtrasCheck = new QCheckBox("Strip unused EE fields");
    m_stripEEExtrasCheck->setToolTip("Remove wirecolor, specular, and shininess from output");
    m_placeableTransCheck = new QCheckBox("Placeable with transparency");
    m_placeableTransCheck->setToolTip("Set transparency hint on meshes matching the key");
    m_transparencyKeyEdit = new QLineEdit("glass");
    m_transparencyKeyEdit->setToolTip("Bitmap name substring to match for transparency");
    m_transparencyKeyEdit->setPlaceholderText("bitmap key");

    auto *meshOpsGroup = new QVBoxLayout;
    meshOpsGroup->setSpacing(Layout::RootMargin);
    meshOpsGroup->addWidget(m_forceWhiteCheck);
    meshOpsGroup->addWidget(m_mergeByBitmapCheck);
    meshOpsGroup->addWidget(m_cullInvisibleCheck);
    meshOpsGroup->addWidget(m_standardizeTexture0Check);
    meshOpsGroup->addWidget(m_stripEEExtrasCheck);
    meshOpsGroup->addWidget(m_placeableTransCheck);
    auto *transKeyWidget = new QWidget;
    auto *transKeyRow = new QHBoxLayout(transKeyWidget);
    transKeyRow->setContentsMargins(Layout::IndentLeft, 0, 0, 0);
    transKeyRow->addWidget(new QLabel("Key:"));
    transKeyRow->addWidget(m_transparencyKeyEdit);
    transKeyWidget->setVisible(m_placeableTransCheck->isChecked());
    connect(m_placeableTransCheck, &QCheckBox::toggled, transKeyWidget, &QWidget::setVisible);
    meshOpsGroup->addWidget(transKeyWidget);
    advLayout->addLayout(meshOpsGroup);

    advContent->setLayout(advLayout);
    optionsLayout->addWidget(m_advancedGroup);

    // ── Tile Options (collapsible) ─────────────────────────────────────
    QWidget *tileContent;
    m_tileGroup = createCollapsibleGroup("Tile Options", &tileContent, true);
    auto *tileLayout = new QVBoxLayout;
    tileLayout->setContentsMargins(Layout::GroupMarginH, Layout::GroupMarginTop, Layout::GroupMarginH, Layout::GroupMarginBottom);
    tileLayout->setSpacing(Layout::RootMargin);

    auto *tileForm = makeStandardForm();

    m_sliceHeightSpin = new QDoubleSpinBox; m_sliceHeightSpin->setRange(1, 100); m_sliceHeightSpin->setValue(20.0); m_sliceHeightSpin->setDecimals(1);
    m_sliceHeightSpin->setToolTip("Height at which to slice geometry for tilefade (in 10cm units)");
    tileForm->addRow("Slice height:", m_sliceHeightSpin);

    m_foliageCombo = new QComboBox; fillCombo(m_foliageCombo, Options::Foliage);
    m_foliageCombo->setToolTip("How to handle foliage meshes in tiles");
    m_foliageKeyEdit = new QLineEdit("trefol");
    m_foliageKeyEdit->setToolTip("Bitmap name substring identifying foliage meshes");
    tileForm->addRow("Foliage:", m_foliageCombo);
    tileForm->addRow("Foliage key:", m_foliageKeyEdit);

    m_rotateGroundCombo = new QComboBox; fillCombo(m_rotateGroundCombo, Options::RotateToggle);
    m_rotateGroundCombo->setToolTip("Set rotatetexture flag on ground meshes");
    m_groundKeyEdit = new QLineEdit;
    m_groundKeyEdit->setToolTip("Bitmap name substring identifying ground meshes");
    tileForm->addRow("Ground rotate:", m_rotateGroundCombo);
    tileForm->addRow("Ground key:", m_groundKeyEdit);

    m_chamferCombo = new QComboBox; fillCombo(m_chamferCombo, Options::Chamfer);
    m_chamferCombo->setToolTip("Add or remove chamfer geometry on tile edges");
    m_retileGroundCombo = new QComboBox; fillCombo(m_retileGroundCombo, Options::RetileSize);
    m_retileGroundCombo->setToolTip("Retile ground textures to a different grid size");
    tileForm->addRow("Chamfers:", m_chamferCombo);
    tileForm->addRow("Retile ground:", m_retileGroundCombo);

    m_raiseLowerCombo = new QComboBox; fillCombo(m_raiseLowerCombo, Options::RaiseLower);
    m_raiseLowerCombo->setToolTip("Raise or lower all geometry by a fixed amount");
    m_raiseAmountSpin = new QDoubleSpinBox; m_raiseAmountSpin->setRange(0.01, 10); m_raiseAmountSpin->setValue(1.0); m_raiseAmountSpin->setDecimals(2); m_raiseAmountSpin->setEnabled(false);
    m_raiseAmountSpin->setToolTip("Amount to raise/lower in meters");
    tileForm->addRow("Raise/Lower:", m_raiseLowerCombo);
    tileForm->addRow("Amount (m):", m_raiseAmountSpin);

    tileLayout->addLayout(tileForm);

    connect(m_raiseLowerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_raiseAmountSpin->setEnabled(i > 0);
    });

    tileLayout->addWidget(makeHLine());

    m_waterEnableCheck = new QCheckBox("Water fixups");
    m_waterEnableCheck->setToolTip("Enable water mesh processing for tiles");
    tileLayout->addWidget(m_waterEnableCheck);

    auto *waterWidget = new QWidget;
    auto *waterForm = makeStandardForm();
    waterWidget->setLayout(waterForm);
    waterForm->setContentsMargins(Layout::IndentLeft, 0, 0, 0);
    m_waterKeyEdit = new QLineEdit("water");
    m_waterKeyEdit->setToolTip("Bitmap name substring identifying water meshes");
    m_dynamicWaterCombo = new QComboBox; fillCombo(m_dynamicWaterCombo, Options::DynamicWater);
    m_dynamicWaterCombo->setToolTip("Water animation mode (wavy adds wave displacement)");
    m_waveHeightSpin = new QDoubleSpinBox; m_waveHeightSpin->setRange(0.1, 10); m_waveHeightSpin->setValue(0.1); m_waveHeightSpin->setEnabled(false);
    m_waveHeightSpin->setToolTip("Wave displacement height for wavy water");
    m_rotateWaterCombo = new QComboBox; fillCombo(m_rotateWaterCombo, Options::RotateToggle);
    m_rotateWaterCombo->setToolTip("Set rotatetexture flag on water meshes");
    m_retileWaterCombo = new QComboBox; fillCombo(m_retileWaterCombo, Options::RetileSize);
    m_retileWaterCombo->setToolTip("Retile water textures to a different grid size");

    waterForm->addRow("Key:", m_waterKeyEdit);
    waterForm->addRow("Dynamic:", m_dynamicWaterCombo);
    waterForm->addRow("Wave height:", m_waveHeightSpin);
    waterForm->addRow("Rotate:", m_rotateWaterCombo);
    waterForm->addRow("Retile:", m_retileWaterCombo);

    waterWidget->setVisible(false);
    tileLayout->addWidget(waterWidget);
    connect(m_waterEnableCheck, &QCheckBox::toggled, waterWidget, &QWidget::setVisible);
    connect(m_dynamicWaterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        bool isWavy = m_dynamicWaterCombo->currentData().toString() == "wavy";
        m_waveHeightSpin->setEnabled(isWavy);
        m_retileWaterCombo->setEnabled(!isWavy);
    });

    m_animateSplotchesCheck = new QCheckBox("Animate splotches");
    m_animateSplotchesCheck->setToolTip("Add animation to splotch/decal meshes");
    m_splotchKeyEdit = new QLineEdit;
    m_splotchKeyEdit->setToolTip("Bitmap name substring identifying splotch meshes");
    m_splotchKeyEdit->setPlaceholderText("bitmap key");
    tileLayout->addWidget(m_animateSplotchesCheck);
    auto *splotchKeyRow = new QHBoxLayout;
    splotchKeyRow->setContentsMargins(Layout::IndentLeft, 0, 0, 0);
    splotchKeyRow->addWidget(new QLabel("Key:"));
    splotchKeyRow->addWidget(m_splotchKeyEdit);
    tileLayout->addLayout(splotchKeyRow);

    m_remapWokMatCheck = new QCheckBox("Remap walkmesh material");
    m_remapWokMatCheck->setToolTip("Remap walkmesh material IDs (e.g. change grass to dirt)");
    m_wokMatFromSpin = new QSpinBox; m_wokMatFromSpin->setRange(0, 30);
    m_wokMatFromSpin->setToolTip("Source material ID to remap from");
    m_wokMatToSpin = new QSpinBox; m_wokMatToSpin->setRange(0, 30);
    m_wokMatToSpin->setToolTip("Target material ID to remap to");
    tileLayout->addWidget(m_remapWokMatCheck);
    auto *wokRow = new QHBoxLayout;
    wokRow->setContentsMargins(Layout::IndentLeft, 0, 0, 0);
    wokRow->addWidget(m_wokMatFromSpin);
    wokRow->addWidget(new QLabel(QString::fromUtf8("\xe2\x86\x92")));
    wokRow->addWidget(m_wokMatToSpin);
    wokRow->addStretch();
    tileLayout->addLayout(wokRow);

    tileContent->setLayout(tileLayout);
    optionsLayout->addWidget(m_tileGroup);

    // ── Pivot Options (collapsible) ────────────────────────────────────
    QWidget *pivotContent;
    m_pivotGroup = createCollapsibleGroup("Pivot Options", &pivotContent, true);
    auto *pivotLayout = new QVBoxLayout;
    pivotLayout->setContentsMargins(Layout::GroupMarginH, Layout::GroupMarginTop, Layout::GroupMarginH, Layout::GroupMarginBottom);
    pivotLayout->setSpacing(Layout::DefaultSpacing);

    m_pivotAllowSplitCheck = new QCheckBox("Allow splitting");
    m_pivotAllowSplitCheck->setToolTip("Allow splitting walkmesh faces to fix bad pivots");
    m_pivotBelowZ0Combo = new QComboBox; fillCombo(m_pivotBelowZ0Combo, Options::PivotBelowZ0);
    m_pivotBelowZ0Combo->setToolTip("How to handle pivot points below ground level");
    m_pivotMoveBadCombo = new QComboBox; fillCombo(m_pivotMoveBadCombo, Options::PivotMoveBad);
    m_pivotMoveBadCombo->setToolTip("Where to move pivots that fail validation");
    m_pivotSmoothingCombo = new QComboBox; fillCombo(m_pivotSmoothingCombo, Options::PivotSmoothing);
    m_pivotSmoothingCombo->setToolTip("How smoothing groups affect pivot computation");
    m_pivotMinFacesSpin = new QSpinBox; m_pivotMinFacesSpin->setRange(2, 8); m_pivotMinFacesSpin->setValue(4);
    m_pivotMinFacesSpin->setToolTip("Minimum number of faces per pivot region");
    m_pivotSplitFirstCombo = new QComboBox; fillCombo(m_pivotSplitFirstCombo, Options::PivotSplitFirst);
    m_pivotSplitFirstCombo->setToolTip("Whether to split convex or concave regions first");

    pivotLayout->addWidget(m_pivotAllowSplitCheck);
    auto *pivotForm = makeStandardForm();
    pivotForm->addRow("Below Z=0:", m_pivotBelowZ0Combo);
    pivotForm->addRow("Move bad:", m_pivotMoveBadCombo);
    pivotForm->addRow("Smoothing:", m_pivotSmoothingCombo);
    pivotForm->addRow("Min faces:", m_pivotMinFacesSpin);
    pivotForm->addRow("Split first:", m_pivotSplitFirstCombo);
    pivotLayout->addLayout(pivotForm);

    pivotContent->setLayout(pivotLayout);
    optionsLayout->addWidget(m_pivotGroup);

    optionsLayout->addStretch();

    sidebarLayout->addWidget(scroll, 1);

    // ── Clean button — pinned at bottom of sidebar ─────────────────────
    sidebarLayout->addWidget(makeHLine());

    m_cleanButton = new QPushButton("Clean");
    m_cleanButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_cleanButton->setAccessibleName(tr("Run cleanmodels"));
    m_cleanButton->setToolTip(tr("Run cleanmodels on all files (F5, ⌘↵)"));
    m_cleanButton->setEnabled(false);
    QFont cleanFont = m_cleanButton->font();
    cleanFont.setBold(true);
    m_cleanButton->setFont(cleanFont);
    // Set min height after the bold font is applied so metrics reflect the
    // actual line height of the rendered label, not the default UI font.
    m_cleanButton->setMinimumHeight(Layout::emH(QFontMetrics(m_cleanButton->font()),
                                                Layout::CleanButtonHeightEm));
    sidebarLayout->addWidget(m_cleanButton);

    m_sidebarToggleBtn = new QPushButton(tr("Hide Sidebar"));
    m_sidebarToggleBtn->setFlat(true);
    m_sidebarToggleBtn->setFixedHeight(Layout::emH(QFontMetrics(m_sidebarToggleBtn->font()),
                                                   Layout::SidebarToggleHeightEm));
    m_sidebarToggleBtn->setAccessibleName(tr("Toggle sidebar visibility"));
    m_sidebarToggleBtn->setToolTip(tr("Show or hide the options sidebar (Ctrl+Shift+S)"));
    sidebarLayout->addWidget(m_sidebarToggleBtn);

    // ====================================================================
    //  RIGHT WORKSPACE — table, detail panel, viewport, raw log drawer
    // ====================================================================
    auto *workspaceWidget = new QWidget;
    auto *workspaceLayout = new QVBoxLayout(workspaceWidget);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(Layout::DefaultSpacing);

    // ── Stats row ─────────────────────────────────────────────────────
    auto *statsRow = new QHBoxLayout;
    m_mdlsDetectedLabel = smallLabel("Detected: 0");
    m_mdlsCleanedLabel = smallLabel("Cleaned: 0");
    m_mdlsFailedLabel = smallLabel("Failed: 0");
    statsRow->addWidget(m_mdlsDetectedLabel);
    statsRow->addSpacing(Layout::SectionGap);
    statsRow->addWidget(m_mdlsCleanedLabel);
    statsRow->addSpacing(Layout::SectionGap);
    statsRow->addWidget(m_mdlsFailedLabel);
    statsRow->addStretch();
    workspaceLayout->addLayout(statsRow);

    // ── File table ────────────────────────────────────────────────────
    m_filesTable = new QTableWidget;
    m_filesTable->setAccessibleName(tr("Files table"));
    m_filesTable->setColumnCount(5);
    m_filesTable->setHorizontalHeaderLabels({"File", "Size", "Status", "Fixes", "Time"});
    m_filesTable->setAlternatingRowColors(true);
    m_filesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    {
        const QFontMetrics fm(m_filesTable->font());
        m_filesTable->setColumnWidth(1, Layout::emW(fm, Layout::TableColumnSizeChars));
        m_filesTable->setColumnWidth(2, Layout::emW(fm, Layout::TableColumnStatusChars));
        m_filesTable->setColumnWidth(3, Layout::emW(fm, Layout::TableColumnFixesChars));
        m_filesTable->setColumnWidth(4, Layout::emW(fm, Layout::TableColumnTimeChars));
        m_filesTable->verticalHeader()->setDefaultSectionSize(
            Layout::emH(fm, Layout::TableRowHeightEm));
    }
    QFont headerFont = m_filesTable->horizontalHeader()->font();
    headerFont.setBold(true);
    m_filesTable->horizontalHeader()->setFont(headerFont);
    m_filesTable->horizontalHeader()->setVisible(true);
    m_filesTable->verticalHeader()->setVisible(false);
    m_filesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_filesTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_filesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_filesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // ── Detail panel ─────────────────────────────────────────────────
    m_detailPanel = new QTextBrowser;
    m_detailPanel->setAccessibleName(tr("File details"));
    m_detailPanel->setReadOnly(true);
    m_detailPanel->setHtml(QStringLiteral("<p style='color:") % QLatin1String(LogColor::Placeholder)
                           % QStringLiteral("; font-style:italic;'>Click a file to see details</p>"));
    m_detailPanel->setMinimumHeight(Layout::emH(QFontMetrics(m_detailPanel->font()),
                                                Layout::DetailPanelMinHeightEm));

    // ── Table + detail splitter ──────────────────────────────────────
    m_tableDetailSplitter = new QSplitter(Qt::Vertical);
    m_tableDetailSplitter->addWidget(m_filesTable);
    m_tableDetailSplitter->addWidget(m_detailPanel);
    m_tableDetailSplitter->setStretchFactor(0, 3);
    m_tableDetailSplitter->setStretchFactor(1, 1);

    // ── Raw log drawer (uses existing m_debugTextBrowser) ────────────
    m_debugTextBrowser = new QTextBrowser;
    m_debugTextBrowser->setAccessibleName(tr("Raw cleanmodels log"));
    m_debugTextBrowser->setPlaceholderText("Output from cleanmodels will appear here.\nClick Clean to begin processing.");
    QFont logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    logFont.setPointSizeF(logFont.pointSizeF() * 0.9);
    m_debugTextBrowser->setFont(logFont);

    m_rawLogDrawer = new QWidget;
    auto *drawerLayout = new QVBoxLayout(m_rawLogDrawer);
    drawerLayout->setContentsMargins(0, 0, 0, 0);
    drawerLayout->setSpacing(0);

    auto *drawerTitleBar = new QWidget;
    auto *drawerTitleLayout = new QHBoxLayout(drawerTitleBar);
    drawerTitleLayout->setContentsMargins(Layout::DefaultSpacing, 0,
                                          Layout::CompactSpacing, 0);
    drawerTitleLayout->setSpacing(Layout::CompactSpacing);
    auto *drawerLabel = new QLabel("Raw Log");
    QFont drawerFont = drawerLabel->font();
    drawerFont.setBold(true);
    drawerFont.setPointSizeF(drawerFont.pointSizeF() * 0.85);
    drawerLabel->setFont(drawerFont);
    drawerTitleBar->setFixedHeight(Layout::emH(QFontMetrics(drawerFont),
                                               Layout::DrawerTitleHeightEm));
    auto *drawerCloseBtn = new QPushButton(QString::fromUtf8("\xc3\x97"));
    {
        const int s = Layout::emH(QFontMetrics(drawerCloseBtn->font()),
                                  Layout::DrawerCloseSizeEm);
        drawerCloseBtn->setFixedSize(s, s);
    }
    drawerCloseBtn->setFlat(true);
    drawerTitleLayout->addWidget(drawerLabel);
    drawerTitleLayout->addStretch();
    drawerTitleLayout->addWidget(drawerCloseBtn);

    drawerLayout->addWidget(drawerTitleBar);
    drawerLayout->addWidget(m_debugTextBrowser, 1);

    m_rawLogDrawer->setVisible(false);
    m_rawLogVisible = false;

    connect(drawerCloseBtn, &QPushButton::clicked, this, &MainWindow::toggleRawLog);

    // ── Viewport with toolbar ────────────────────────────────────────
    m_viewport = new ModelViewport(this);
    m_viewport->setAccessibleName(tr("3D model viewport"));
    m_viewport->setAccessibleDescription(tr("Live preview of the selected MDL. Drag to orbit, right-drag to pan, scroll to zoom."));
    m_viewport->setMinimumSize(200, 150);

    auto *viewportContainer = new QWidget;
    auto *viewportVLayout = new QVBoxLayout(viewportContainer);
    viewportVLayout->setContentsMargins(0, 0, 0, 0);
    viewportVLayout->setSpacing(Layout::CompactSpacing);

    auto *viewToolbar = new QHBoxLayout;
    m_wireframeCheck = new QCheckBox("Wireframe");
    m_wireframeCheck->setAccessibleName(tr("Wireframe rendering"));
    m_gridCheck = new QCheckBox("Grid");
    m_gridCheck->setAccessibleName(tr("Show ground grid"));
    m_gridCheck->setChecked(true);
    m_refModelCheck = new QCheckBox("Reference:");
    m_refModelCheck->setAccessibleName(tr("Show reference model overlay"));
    m_refModelCombo = new QComboBox;
    m_refModelCombo->setAccessibleName(tr("Reference model"));
    m_refModelCombo->setMinimumWidth(Layout::emW(QFontMetrics(m_refModelCombo->font()),
                                                 Layout::RefModelComboWidthChars));
    m_refModelCombo->setEditable(false);
    m_refModelCombo->addItem("(none)");
    m_refModelCombo->setEnabled(false);
    m_refBrowseBtn = new QPushButton("Browse…");
    m_refBrowseBtn->setAccessibleName(tr("Browse for reference model file"));
    m_refBrowseBtn->setEnabled(false);

    viewToolbar->setSpacing(Layout::RootMargin);
    viewToolbar->addWidget(m_wireframeCheck);
    viewToolbar->addWidget(m_gridCheck);
    viewToolbar->addSpacing(Layout::SectionGap);
    viewToolbar->addWidget(m_refModelCheck);
    viewToolbar->addWidget(m_refModelCombo);
    viewToolbar->addWidget(m_refBrowseBtn);
    viewToolbar->addStretch();
    viewportVLayout->addLayout(viewToolbar);
    viewportVLayout->addWidget(m_viewport, 1);

    // ── Vertical splitter: table+detail (top) | viewport+drawer (bottom)
    auto *workspaceSplitter = new QSplitter(Qt::Vertical);
    workspaceSplitter->addWidget(m_tableDetailSplitter);
    auto *viewportAndDrawer = new QWidget;
    auto *viewportAndDrawerLayout = new QVBoxLayout(viewportAndDrawer);
    viewportAndDrawerLayout->setContentsMargins(0, 0, 0, 0);
    viewportAndDrawerLayout->setSpacing(0);
    viewportAndDrawerLayout->addWidget(viewportContainer, 1);
    viewportAndDrawerLayout->addWidget(m_rawLogDrawer);
    workspaceSplitter->addWidget(viewportAndDrawer);
    workspaceSplitter->setStretchFactor(0, 2);
    workspaceSplitter->setStretchFactor(1, 3);
    workspaceLayout->addWidget(workspaceSplitter, 1);

    // ====================================================================
    //  MAIN SPLITTER — sidebar | workspace
    // ====================================================================
    m_mainSplitter = new QSplitter(Qt::Horizontal, central);
    m_mainSplitter->addWidget(m_sidebarWidget);
    m_mainSplitter->addWidget(workspaceWidget);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({Layout::SidebarDefaultWidth, 700});

    root->addWidget(m_mainSplitter, 1);

    // ====================================================================
    //  SIGNALS — identical to before
    // ====================================================================
    connect(m_wireframeCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_viewport->setWireframe(on);
    });
    connect(m_gridCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_viewport->setShowGrid(on);
    });
    connect(m_refModelCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_refModelCombo->setEnabled(on);
        m_refBrowseBtn->setEnabled(on);
        if (!on) {
            m_viewport->clearReference();
        } else if (m_refModelCombo->currentIndex() > 0) {
            m_viewport->loadReferenceFile(m_refModelCombo->currentData().toString());
        }
    });
    connect(m_refBrowseBtn, &QPushButton::clicked, this, [this] {
        QString file = QFileDialog::getOpenFileName(this, "Reference Model",
            m_sInDir.isEmpty() ? QDir::currentPath() : m_sInDir, "MDL Files (*.mdl)");
        if (file.isEmpty()) return;
        QFileInfo fi(file);
        QString label = fi.fileName();
        int idx = m_refModelCombo->findData(file);
        if (idx < 0) {
            m_refModelCombo->addItem(label, file);
            idx = m_refModelCombo->count() - 1;
        }
        m_refModelCombo->setCurrentIndex(idx);
        m_viewport->loadReferenceFile(file);
    });
    connect(m_refModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!m_refModelCheck->isChecked()) return;
        if (idx <= 0) {
            m_viewport->clearReference();
        } else {
            QString path = m_refModelCombo->currentData().toString();
            if (!path.isEmpty())
                m_viewport->loadReferenceFile(path);
        }
    });

    connect(m_inDirectory, &QLineEdit::editingFinished, this, &MainWindow::populateRefModelCombo);

    connect(m_radioDecompile, &QRadioButton::toggled, this, &MainWindow::updateModeUI);
    connect(m_radioCompile, &QRadioButton::toggled, this, &MainWindow::updateModeUI);

    connect(m_filesTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (m_filesTable->selectedItems().isEmpty()) return;

        QStringList selectedFiles = collectTableFilePaths(false);
        QString firstFile = selectedFiles.isEmpty() ? QString() : selectedFiles.first();

        QMenu menu;
        if (selectedFiles.size() == 1) {
            menu.addAction("Preview in viewport", this, [this, firstFile] {
                if (m_viewport && !firstFile.isEmpty())
                    m_viewport->previewFile(firstFile);
            });
        }
        menu.addAction("Copy path to clipboard", this, &MainWindow::copyToClipboard);
        menu.addSeparator();

        QString reportLabel = selectedFiles.size() == 1
            ? "Report Issue..."
            : QString("Report Issue (%1 files)...").arg(selectedFiles.size());
        menu.addAction(reportLabel, this, [this, selectedFiles] {
            reportIssueInteractive(selectedFiles);
        });
        menu.addSeparator();

        if (selectedFiles.size() == 1) {
            menu.addAction("Reveal in Finder", this, [firstFile] {
                QProcess::startDetached("open", {"-R", firstFile});
            });
        }
        menu.exec(m_filesTable->mapToGlobal(pos));
    });

    connect(m_filesTable, &QTableWidget::doubleClicked, this, [this](const QModelIndex &index) {
        QString cellText = index.siblingAtColumn(0).data(Qt::UserRole).toString();
        m_debugTextBrowser->moveCursor(QTextCursor::Start);
        if (!m_debugTextBrowser->find(cellText))
            m_debugTextBrowser->moveCursor(QTextCursor::End);
        if (m_viewport && !cellText.isEmpty() && !m_sInDir.isEmpty())
            m_viewport->previewFile(m_sInDir + "/" + cellText);
    });

    connect(m_indirButton, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "Input Directory", m_sInDir);
        if (!dir.isEmpty()) onUpdateInDir(dir);
    });
    connect(m_outdirButton, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "Output Directory", m_sOutDir);
        if (!dir.isEmpty()) { m_outDirectory->setText(dir); m_sOutDir = dir; }
    });
    connect(m_inDirectory, &QLineEdit::editingFinished, this, [this] {
        QFileInfo fi(m_inDirectory->text());
        if (m_inDirectory->text().isEmpty()) {
            m_inDirectory->setStyleSheet("");
            m_inDirectory->setToolTip("");
            m_cleanButton->setEnabled(false);
        } else if (fi.exists() && fi.isDir()) {
            m_inDirectory->setStyleSheet("");
            m_inDirectory->setToolTip(fi.absoluteFilePath());
            m_cleanButton->setEnabled(true);
            onUpdateInDir(m_inDirectory->text());
        } else {
            m_inDirectory->setStyleSheet(QStringLiteral("color: ") + LogColor::InvalidPath);
            m_inDirectory->setToolTip("Directory does not exist");
            m_cleanButton->setEnabled(false);
        }
    });
    connect(m_outDirectory, &QLineEdit::editingFinished, this, [this] { m_sOutDir = m_outDirectory->text(); });
    connect(m_filePattern, &QLineEdit::textChanged, this, [this] {
        m_dirWatcherTimer->stop();
        m_bFilesHaveChanged = false;
        updateFileListing();
        m_dirWatcherTimer->start();
    });
    m_cleanButton->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_cleanButton, &QPushButton::clicked, this, [this] { doClean(); });

    // Ctrl+Return alternate trigger — F5 isn't reachable on laptops with
    // function-locked keyboards (Apple notebooks, many ultrabooks). Mirrors
    // the macOS convention of ⌘↵ for "primary action in this window".
    auto *cleanReturnShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    cleanReturnShortcut->setContext(Qt::WindowShortcut);
    connect(cleanReturnShortcut, &QShortcut::activated, this, [this] {
        if (m_cleanButton->isEnabled()) doClean();
    });
    auto *cleanEnterShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this);
    cleanEnterShortcut->setContext(Qt::WindowShortcut);
    connect(cleanEnterShortcut, &QShortcut::activated, this, [this] {
        if (m_cleanButton->isEnabled()) doClean();
    });

    // Esc aborts an in-flight run. No-op when nothing is running, so it
    // doesn't steal focus from dialogs or text fields with their own Esc
    // handling (Qt::WindowShortcut respects modal dialogs above this window).
    auto *abortShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    abortShortcut->setContext(Qt::WindowShortcut);
    connect(abortShortcut, &QShortcut::activated, this, [this] {
        if (m_bCleanRunning) doClean();
    });

    // ── Detail panel / sidebar / raw log connections ─────────────────
    connect(m_filesTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0 && row < m_filesTable->rowCount()) {
            QString fileName = m_filesTable->item(row, 0)->data(Qt::UserRole).toString();
            showFileDetails(fileName);
        }
    });

    connect(m_sidebarToggleBtn, &QPushButton::clicked, this, &MainWindow::toggleSidebar);
    connect(ui->actionToggleSidebar, &QAction::triggered, this, &MainWindow::toggleSidebar);
    connect(ui->actionToggleRawLog, &QAction::triggered, this, &MainWindow::toggleRawLog);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Icons
    m_iconCleanButton     = style()->standardIcon(QStyle::SP_MediaPlay);
    m_iconAbortButton     = style()->standardIcon(QStyle::SP_DialogCancelButton);
    m_iconDecompileButton = style()->standardIcon(QStyle::SP_FileIcon);
    ui->actionLoadPreset->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    ui->actionSavePreset->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    ui->actionHelp->setIcon(style()->standardIcon(QStyle::SP_DialogHelpButton));

    m_sBinaryName = CliDefaults::BinaryName;

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
        m_sBinaryPath = cliInPath;

    // Build the UI
    buildUi();

    if (cliFound)
    {
        appendDebugHtml("Clean Models:EE ready.<br>");
        appendDebugHtml("CLI: " + m_sBinaryPath.toHtmlEscaped() + "<br>");

        // Populate the validation check tree from CLI metadata
        auto *listProc = new QProcess(this);
        connect(listProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, listProc](int exitCode, QProcess::ExitStatus) {
            listProc->deleteLater();
            if (exitCode != 0) return;
            QByteArray output = listProc->readAllStandardOutput();
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(output, &err);
            if (err.error != QJsonParseError::NoError || !doc.isArray()) return;
            populateCheckTree(doc.array());
        });
        listProc->start(m_sBinaryPath, {"check", "--list"});
    }
    else
    {
        QString errorMsg = "Could not find the " % m_sBinaryName % " executable in the current directory or in your path!";
        QMessageBox::critical(this, "No cleanmodels CLI", errorMsg);
        appendDebugHtml(LogHtml::span(LogColor::Error, errorMsg.toHtmlEscaped()) % QStringLiteral("<br>"));
    }

    m_viewport->setCliBinaryPath(m_sBinaryPath);

    connect(m_viewport, &ModelViewport::previewError, this, [this](const QString &msg) {
        appendDebugHtml(LogHtml::logLine(LogColor::Warning, QStringLiteral("Preview: ") % msg.toHtmlEscaped()));
    });

    // Process
    m_pCleanProcess = new QProcess(this);
    m_bCleanRunning = false;

    // Status bar
    auto *sStatusLabel = new QLabel("Status:");
    m_pCleanStatus = new QLabel("Idle");
    m_pStatusProgress = new QProgressBar;
    m_pStatusProgress->setRange(0, 0);
    m_pStatusProgress->setTextVisible(false);
    m_pStatusProgress->setVisible(false);
    {
        const QFontMetrics fm(m_pStatusProgress->font());
        m_pStatusProgress->setMaximumHeight(Layout::emH(fm, Layout::StatusProgressHeightEm));
        m_pStatusProgress->setMaximumWidth(Layout::emW(fm, Layout::StatusProgressWidthChars));
    }
    statusBar()->addPermanentWidget(sStatusLabel);
    statusBar()->addPermanentWidget(m_pCleanStatus);
    statusBar()->addPermanentWidget(m_pStatusProgress);

    // Dir completer
    m_pDirCompleter = new QCompleter(this);
    m_pDirCompleter->setMaxVisibleItems(4);
    m_pFileSystemModel = new FileSystemModel(m_pDirCompleter);
    m_pFileSystemModel->setFilter(QDir::Dirs | QDir::Drives | QDir::NoDotAndDotDot | QDir::AllDirs);
    m_pDirCompleter->setModel(m_pFileSystemModel);
    m_inDirectory->setCompleter(m_pDirCompleter);
    m_outDirectory->setCompleter(m_pDirCompleter);

    // Dir watcher
    m_dirWatcherTimer = new QTimer(this);
    m_dirWatcherTimer->setInterval(500);
    connect(m_dirWatcherTimer, &QTimer::timeout, this, &MainWindow::handleDirWatcherTimer);
    m_bUpdateFilesAfterClean = false;

    // Signals
    connect(m_pCleanProcess, &QProcess::started, this, &MainWindow::onCleanStarted);
    connect(m_pCleanProcess, &QProcess::errorOccurred, this, &MainWindow::onCleanProcessError);
    connect(m_pCleanProcess, &QProcess::finished, this, &MainWindow::onCleanFinished);
    connect(ui->actionHelp, &QAction::triggered, this, &MainWindow::onHelpTriggered);
    connect(ui->actionReportIssue, &QAction::triggered, this, &MainWindow::onReportIssueTriggered);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAboutTriggered);
    connect(ui->actionSavePreset, &QAction::triggered, this, &MainWindow::onSaveConfigTriggered);
    connect(ui->actionLoadPreset, &QAction::triggered, this, &MainWindow::onLoadConfigTriggered);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::onQuitTriggered);
    connect(&m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onDirectoryContentsChanged);

    setAcceptDrops(true);

    loadSettings();
    readSettings();
}

MainWindow::~MainWindow()
{
    if (m_pCleanProcess->state() != QProcess::NotRunning) {
        m_pCleanProcess->kill();
        m_pCleanProcess->waitForFinished(3000);
    }
    delete ui;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void MainWindow::readSettings()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (const QWindow *window = windowHandle())
        screen = window->screen();
    if (!screen) return;

    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    const QByteArray geometry = settings.value(Setting::Geometry, QByteArray()).toByteArray();
    if (geometry.isEmpty())
    {
        const QRect avail = screen->availableGeometry();
        resize(avail.width() / 2, avail.height() * 2 / 3);
        move((avail.width() - width()) / 2, (avail.height() - height()) / 2);
    }
    else
        restoreGeometry(geometry);
}

void MainWindow::writeSettings()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue(Setting::Geometry, saveGeometry());
}

void MainWindow::loadSettings()
{
    QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    s.beginGroup(Setting::OptionsGroup);

    QString inDir = s.value(Setting::InDir).toString();
    if (!inDir.isEmpty())
    {
        onUpdateInDir(inDir);
        QDir absDir;
        m_pFileSystemModel->setRootPath(absDir.absoluteFilePath(inDir));
    }

    QString outDir = s.value(Setting::OutDir).toString();
    if (!outDir.isEmpty())
    {
        m_sOutDir = outDir;
        m_outDirectory->setText(m_sOutDir);
    }

    m_filePattern->setText(s.value(Setting::Pattern, "*.mdl").toString());
    m_classificationCombo->setCurrentIndex(s.value(Setting::Classification, 0).toInt());

    // Fixes
    m_allFixesCheck->setChecked(s.value(Setting::AllFixes, true).toBool());
    m_checkValidateAll->setChecked(s.value(Setting::FixValidate, true).toBool());
    m_checkStripDegen->setChecked(s.value(Setting::FixStripDegen, true).toBool());
    m_checkFixAnims->setChecked(s.value(Setting::FixAnimations, true).toBool());
    m_checkRepairPivots->setChecked(s.value(Setting::FixPivots, true).toBool());
    m_checkFixTilefade->setChecked(s.value(Setting::FixTilefade, true).toBool());
    m_checkTilefadeUndo->setChecked(s.value(Setting::TilefadeUndo, false).toBool());
    m_checkRebuildAABB->setChecked(s.value(Setting::FixAabb, true).toBool());
    m_checkReparentChildren->setChecked(s.value(Setting::FixReparent, true).toBool());
    m_checkWrapRoot->setChecked(s.value(Setting::FixWrapRoot, true).toBool());
    m_checkSplitMultiEdge->setChecked(s.value(Setting::FixSplitMultiedge, true).toBool());

    // Advanced
    m_scaleXSpin->setValue(s.value(Setting::RescaleX, 1.0).toDouble());
    m_scaleYSpin->setValue(s.value(Setting::RescaleY, 1.0).toDouble());
    m_scaleZSpin->setValue(s.value(Setting::RescaleZ, 1.0).toDouble());
    m_snapCombo->setCurrentIndex(s.value(Setting::Snap, 0).toInt());
    m_tvertSnapCombo->setCurrentIndex(s.value(Setting::TvertSnap, 0).toInt());
    m_renderCombo->setCurrentIndex(s.value(Setting::RenderMode, 0).toInt());
    m_shadowCombo->setCurrentIndex(s.value(Setting::ShadowMode, 0).toInt());
    m_forceWhiteCheck->setChecked(s.value(Setting::ForceWhite, false).toBool());
    m_mergeByBitmapCheck->setChecked(s.value(Setting::MergeByBitmap, false).toBool());
    m_cullInvisibleCheck->setChecked(s.value(Setting::InvisibleMeshCull, false).toBool());
    m_standardizeTexture0Check->setChecked(s.value(Setting::StandardizeTexture0, false).toBool());
    m_stripEEExtrasCheck->setChecked(s.value(Setting::StripEEExtras, false).toBool());
    m_placeableTransCheck->setChecked(s.value(Setting::PlaceableTrans, false).toBool());
    m_transparencyKeyEdit->setText(s.value(Setting::TransparencyKey, "glass").toString());

    // Tiles
    m_sliceHeightSpin->setValue(s.value(Setting::SliceHeight, 20.0).toDouble());
    m_waterEnableCheck->setChecked(s.value(Setting::DoWater, false).toBool());
    m_dynamicWaterCombo->setCurrentIndex(s.value(Setting::DynamicWater, 0).toInt());
    m_waveHeightSpin->setValue(s.value(Setting::WaveHeight, 0.1).toDouble());
    m_waterKeyEdit->setText(s.value(Setting::WaterKey, "water").toString());
    m_rotateWaterCombo->setCurrentIndex(s.value(Setting::RotateWater, 0).toInt());
    m_retileWaterCombo->setCurrentIndex(s.value(Setting::TileWater, 0).toInt());
    m_foliageCombo->setCurrentIndex(s.value(Setting::Foliage, 0).toInt());
    m_foliageKeyEdit->setText(s.value(Setting::FoliageKey, "trefol").toString());
    m_animateSplotchesCheck->setChecked(s.value(Setting::AnimateSplotches, false).toBool());
    m_splotchKeyEdit->setText(s.value(Setting::SplotchKey).toString());
    m_rotateGroundCombo->setCurrentIndex(s.value(Setting::RotateGround, 0).toInt());
    m_chamferCombo->setCurrentIndex(s.value(Setting::ChamferMode, 0).toInt());
    m_retileGroundCombo->setCurrentIndex(s.value(Setting::TileGround, 0).toInt());
    m_groundKeyEdit->setText(s.value(Setting::GroundKey).toString());
    m_raiseLowerCombo->setCurrentIndex(s.value(Setting::TileRaise, 0).toInt());
    m_raiseAmountSpin->setValue(s.value(Setting::TileRaiseAmount, 1.0).toDouble());
    m_remapWokMatCheck->setChecked(s.value(Setting::MapAabbMaterial, false).toBool());
    m_wokMatFromSpin->setValue(s.value(Setting::MapAabbFrom, 0).toInt());
    m_wokMatToSpin->setValue(s.value(Setting::MapAabbTo, 0).toInt());

    // Pivots
    m_pivotAllowSplitCheck->setChecked(s.value(Setting::AllowSplit, false).toBool());
    m_pivotBelowZ0Combo->setCurrentIndex(s.value(Setting::PivotsBelowZ0, 0).toInt());
    m_pivotMoveBadCombo->setCurrentIndex(s.value(Setting::MoveBadPivots, 0).toInt());
    m_pivotSmoothingCombo->setCurrentIndex(s.value(Setting::SmoothingGroups, 0).toInt());
    m_pivotMinFacesSpin->setValue(s.value(Setting::MinSize, 4).toInt());
    m_pivotSplitFirstCombo->setCurrentIndex(s.value(Setting::SplitFirst, 0).toInt());

    // Camera
    if (m_viewport) {
        Camera &cam = m_viewport->camera();
        cam.setRotationSensitivity(s.value(Setting::CameraRotSens, Camera::DefaultRotationSensitivity).toFloat());
        cam.setPanScale(s.value(Setting::CameraPanScale, Camera::DefaultPanScale).toFloat());
        cam.setZoomFactor(s.value(Setting::CameraZoomFactor, Camera::DefaultZoomFactor).toFloat());
    }

    // Layout visibility
    bool sidebarVis = s.value(Setting::SidebarVisible, true).toBool();
    m_sidebarWidget->setVisible(sidebarVis);
    m_sidebarToggleBtn->setText(sidebarVis ? tr("Hide Sidebar") : tr("Show Sidebar"));
    m_sidebarToggleBtn->setVisible(!sidebarVis);

    m_rawLogVisible = s.value(Setting::RawLogVisible, false).toBool();
    m_rawLogDrawer->setVisible(m_rawLogVisible);

    s.endGroup();
}

void MainWindow::saveSettings()
{
    QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    s.beginGroup(Setting::OptionsGroup);

    s.setValue(Setting::InDir, m_sInDir);
    s.setValue(Setting::OutDir, m_sOutDir);
    s.setValue(Setting::Pattern, m_filePattern->text());
    s.setValue(Setting::Classification, m_classificationCombo->currentIndex());

    s.setValue(Setting::AllFixes, m_allFixesCheck->isChecked());
    s.setValue(Setting::FixValidate, m_checkValidateAll->isChecked());
    s.setValue(Setting::FixStripDegen, m_checkStripDegen->isChecked());
    s.setValue(Setting::FixAnimations, m_checkFixAnims->isChecked());
    s.setValue(Setting::FixPivots, m_checkRepairPivots->isChecked());
    s.setValue(Setting::FixTilefade, m_checkFixTilefade->isChecked());
    s.setValue(Setting::TilefadeUndo, m_checkTilefadeUndo->isChecked());
    s.setValue(Setting::FixAabb, m_checkRebuildAABB->isChecked());
    s.setValue(Setting::FixReparent, m_checkReparentChildren->isChecked());
    s.setValue(Setting::FixWrapRoot, m_checkWrapRoot->isChecked());
    s.setValue(Setting::FixSplitMultiedge, m_checkSplitMultiEdge->isChecked());

    s.setValue(Setting::RescaleX, m_scaleXSpin->value());
    s.setValue(Setting::RescaleY, m_scaleYSpin->value());
    s.setValue(Setting::RescaleZ, m_scaleZSpin->value());
    s.setValue(Setting::Snap, m_snapCombo->currentIndex());
    s.setValue(Setting::TvertSnap, m_tvertSnapCombo->currentIndex());
    s.setValue(Setting::RenderMode, m_renderCombo->currentIndex());
    s.setValue(Setting::ShadowMode, m_shadowCombo->currentIndex());
    s.setValue(Setting::ForceWhite, m_forceWhiteCheck->isChecked());
    s.setValue(Setting::MergeByBitmap, m_mergeByBitmapCheck->isChecked());
    s.setValue(Setting::InvisibleMeshCull, m_cullInvisibleCheck->isChecked());
    s.setValue(Setting::StandardizeTexture0, m_standardizeTexture0Check->isChecked());
    s.setValue(Setting::StripEEExtras, m_stripEEExtrasCheck->isChecked());
    s.setValue(Setting::PlaceableTrans, m_placeableTransCheck->isChecked());
    s.setValue(Setting::TransparencyKey, m_transparencyKeyEdit->text());

    s.setValue(Setting::SliceHeight, m_sliceHeightSpin->value());
    s.setValue(Setting::DoWater, m_waterEnableCheck->isChecked());
    s.setValue(Setting::DynamicWater, m_dynamicWaterCombo->currentIndex());
    s.setValue(Setting::WaveHeight, m_waveHeightSpin->value());
    s.setValue(Setting::WaterKey, m_waterKeyEdit->text());
    s.setValue(Setting::RotateWater, m_rotateWaterCombo->currentIndex());
    s.setValue(Setting::TileWater, m_retileWaterCombo->currentIndex());
    s.setValue(Setting::Foliage, m_foliageCombo->currentIndex());
    s.setValue(Setting::FoliageKey, m_foliageKeyEdit->text());
    s.setValue(Setting::AnimateSplotches, m_animateSplotchesCheck->isChecked());
    s.setValue(Setting::SplotchKey, m_splotchKeyEdit->text());
    s.setValue(Setting::RotateGround, m_rotateGroundCombo->currentIndex());
    s.setValue(Setting::ChamferMode, m_chamferCombo->currentIndex());
    s.setValue(Setting::TileGround, m_retileGroundCombo->currentIndex());
    s.setValue(Setting::GroundKey, m_groundKeyEdit->text());
    s.setValue(Setting::TileRaise, m_raiseLowerCombo->currentIndex());
    s.setValue(Setting::TileRaiseAmount, m_raiseAmountSpin->value());
    s.setValue(Setting::MapAabbMaterial, m_remapWokMatCheck->isChecked());
    s.setValue(Setting::MapAabbFrom, m_wokMatFromSpin->value());
    s.setValue(Setting::MapAabbTo, m_wokMatToSpin->value());

    s.setValue(Setting::AllowSplit, m_pivotAllowSplitCheck->isChecked());
    s.setValue(Setting::PivotsBelowZ0, m_pivotBelowZ0Combo->currentIndex());
    s.setValue(Setting::MoveBadPivots, m_pivotMoveBadCombo->currentIndex());
    s.setValue(Setting::SmoothingGroups, m_pivotSmoothingCombo->currentIndex());
    s.setValue(Setting::MinSize, m_pivotMinFacesSpin->value());
    s.setValue(Setting::SplitFirst, m_pivotSplitFirstCombo->currentIndex());

    // Camera
    if (m_viewport) {
        const Camera &cam = m_viewport->camera();
        s.setValue(Setting::CameraRotSens, static_cast<double>(cam.rotationSensitivity()));
        s.setValue(Setting::CameraPanScale, static_cast<double>(cam.panScale()));
        s.setValue(Setting::CameraZoomFactor, static_cast<double>(cam.zoomFactor()));
    }

    // Layout visibility
    s.setValue(Setting::SidebarVisible, m_sidebarWidget->isVisible());
    s.setValue(Setting::RawLogVisible, m_rawLogVisible);

    s.endGroup();
}

void MainWindow::closeEvent(QCloseEvent *)
{
    writeSettings();
    saveSettings();
}

// ---------------------------------------------------------------------------
// Menu actions
// ---------------------------------------------------------------------------
void MainWindow::onLoadConfigTriggered()
{
    QFileDialog fileDialog(this);
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setNameFilters({"Clean Models Config (*.ini)", "Legacy Config (*.cm *.pl)"});
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
    QFileDialog fileDialog(this);
    fileDialog.setAcceptMode(QFileDialog::AcceptSave);
    fileDialog.setFileMode(QFileDialog::AnyFile);
    fileDialog.setDefaultSuffix("ini");
    fileDialog.setNameFilters({"Clean Models Config (*.ini)"});
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
    QMessageBox::about(this, "About Clean Models:EE",
                       "Clean Models:EE\n\n"
                       "A tool to validate, repair, and compile 3D models\n"
                       "for Neverwinter Nights: Enhanced Edition.\n\n"
                       "Powered by cleanmodels (Go CLI).\n"
                       "Press F5 to clean.");
}

void MainWindow::onHelpTriggered() { QWhatsThis::enterWhatsThisMode(); }

QStringList MainWindow::collectTableFilePaths(bool allRows) const
{
    QStringList paths;
    if (allRows) {
        for (int i = 0; i < m_filesTable->rowCount(); ++i) {
            auto *item = m_filesTable->item(i, 0);
            if (item)
                paths << (m_sInDir + "/" + item->data(Qt::UserRole).toString());
        }
    } else {
        QSet<int> rows;
        for (auto *item : m_filesTable->selectedItems())
            rows.insert(item->row());
        for (int row : rows) {
            auto *item = m_filesTable->item(row, 0);
            if (item)
                paths << (m_sInDir + "/" + item->data(Qt::UserRole).toString());
        }
    }
    return paths;
}

void MainWindow::onReportIssueTriggered()
{
    QStringList filesToReport;

    // Prefer last failed files, then selected table rows
    if (!m_lastFailedFiles.isEmpty()) {
        filesToReport = m_lastFailedFiles;
    } else {
        filesToReport = collectTableFilePaths(false);
    }

    if (filesToReport.isEmpty()) {
        QMessageBox::information(this, "Report Issue",
            "Select one or more files in the table first, or run cleanmodels so failed files can be reported.");
        return;
    }

    if (!m_lastErrorOutput.isEmpty()) {
        reportIssue(filesToReport, m_lastErrorOutput, m_lastCommand);
    } else {
        reportIssueInteractive(filesToReport);
    }
}

void MainWindow::reportIssue(const QStringList &files, const QString &errorOutput, const QString &command)
{
    if (m_sBinaryPath.isEmpty()) {
        QMessageBox::warning(this, "Report Issue",
            "Cannot find the cleanmodels CLI binary.\n"
            "The report command requires the CLI to be available.");
        return;
    }

    QStringList args;
    args << "report";
    args << "--error" << errorOutput;
    if (!command.isEmpty())
        args << "--command" << command;

    for (const auto &f : files) {
        if (QFile::exists(f))
            args << f;
    }

    if (args.count() <= 4) {
        QMessageBox::warning(this, "Report Issue",
            "None of the failed model files could be found on disk.\n"
            "They may have been moved or deleted.");
        return;
    }

    auto confirm = QMessageBox::question(this, "Report Issue",
        QString("Submit a bug report with %1 file(s)?\n\n"
                "This will send the model file(s) and error details to the cleanmodels issue tracker.\n"
                "No GitHub account is needed.")
            .arg(files.size()),
        QMessageBox::Yes | QMessageBox::Cancel);

    if (confirm != QMessageBox::Yes)
        return;

    appendDebugHtml(LogHtml::span(LogColor::Info, QStringLiteral("Submitting bug report...")) % QStringLiteral("<br>"));

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
        QString output = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        QString errors = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();

        if (exitCode == 0 && !output.isEmpty()) {
            QString issueUrl = output.split('\n').last().trimmed();
            appendDebugHtml(LogHtml::span(LogColor::Success,
                QStringLiteral("Report submitted: <a href=\"") % issueUrl.toHtmlEscaped()
                % QStringLiteral("\">") % issueUrl.toHtmlEscaped() % QStringLiteral("</a>"))
                % QStringLiteral("<br>"));
            m_detailPanel->setHtml(
                LogHtml::detailLine(LogColor::FixApplied, QStringLiteral("<b>Report submitted successfully!</b>"))
                % QStringLiteral("<p><a href='") % issueUrl.toHtmlEscaped() % QStringLiteral("'>")
                % issueUrl.toHtmlEscaped() % QStringLiteral("</a></p>"));
        } else {
            QString msg = errors.isEmpty() ? "Unknown error" : errors;
            appendDebugHtml(LogHtml::span(LogColor::Error,
                QStringLiteral("Report failed: ") % msg.toHtmlEscaped()) % QStringLiteral("<br>"));
            QMessageBox::warning(this, "Report Failed",
                "Failed to submit the bug report:\n\n" + msg);
        }
    });

    proc->start(m_sBinaryPath, args);
}

void MainWindow::reportIssueInteractive(const QStringList &files)
{
    // Validate total size before prompting user
    qint64 totalSize = 0;
    QStringList validFiles;
    for (const auto &f : files) {
        QFileInfo fi(f);
        if (!fi.exists()) continue;
        if (fi.size() > ReportLimits::MaxFileSize) {
            QMessageBox::warning(this, "Report Issue",
                QString("File '%1' exceeds the 10MB per-file limit and cannot be reported.")
                    .arg(fi.fileName()));
            return;
        }
        totalSize += fi.size();
        validFiles << f;
    }

    if (validFiles.isEmpty()) {
        QMessageBox::warning(this, "Report Issue",
            "None of the selected files could be found on disk.");
        return;
    }

    if (totalSize > ReportLimits::MaxTotalSize) {
        QMessageBox::warning(this, "Report Issue",
            QString("Total size of selected files (%1) exceeds the 25MB limit.\n"
                    "Try selecting fewer files.")
                .arg(humanFileSize(totalSize)));
        return;
    }

    bool ok = false;
    QString description = QInputDialog::getMultiLineText(this, "Report Issue",
        QString("Describe the problem with %1 file(s) (%2 total):\n\n"
                "Examples: \"missing faces after decompile\", \"textures wrong after compile\",\n"
                "\"model looks broken in-game\"\n\n"
                "Leave blank if you just want us to look at the file(s).")
            .arg(validFiles.size())
            .arg(humanFileSize(totalSize)),
        QString(), &ok);

    if (!ok)
        return;

    if (description.isEmpty())
        description = "User-reported issue (no specific error — model may produce incorrect output)";

    reportIssue(validFiles, description, m_lastCommand);
}

void MainWindow::populateCheckTree(const QJsonArray &checks)
{
    // Clear any previous dynamic content
    m_categoryChecks.clear();
    m_categoryWidgets.clear();
    m_individualChecks.clear();

    QLayoutItem *child;
    while ((child = m_checksDetailWidget->layout()->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    // Group checks by category preserving insertion order
    QStringList categoryOrder;
    QMap<QString, QJsonArray> byCategory;
    for (const auto &val : checks) {
        QJsonObject obj = val.toObject();
        QString cat = obj["category"].toString();
        if (!byCategory.contains(cat))
            categoryOrder << cat;
        byCategory[cat].append(obj);
    }

    auto *layout = qobject_cast<QVBoxLayout*>(m_checksDetailWidget->layout());

    for (const QString &cat : categoryOrder) {
        const QJsonArray &catChecks = byCategory[cat];
        QString label = cat.at(0).toUpper() + cat.mid(1)
                        + QString(" (%1 checks)").arg(catChecks.size());

        auto *catCheck = new QCheckBox(label);
        catCheck->setChecked(true);
        catCheck->setToolTip(QString("Enable/disable all %1 checks").arg(cat));
        QFont f = catCheck->font();
        f.setBold(true);
        catCheck->setFont(f);
        layout->addWidget(catCheck);
        m_categoryChecks[cat] = catCheck;

        auto *catContent = new QWidget;
        auto *catLayout = new QVBoxLayout(catContent);
        catLayout->setContentsMargins(Layout::IndentLeft, 0, 0, 0);
        catLayout->setSpacing(Layout::TightSpacing);
        catContent->setVisible(false);
        layout->addWidget(catContent);
        m_categoryWidgets[cat] = catContent;

        for (const auto &c : catChecks) {
            QJsonObject obj = c.toObject();
            QString name = obj["name"].toString();
            QString desc = obj["description"].toString();
            bool fixable = obj["fixable"].toBool();

            QString checkLabel = name;
            if (!desc.isEmpty())
                checkLabel += QString::fromUtf8(" \xe2\x80\x94 ") + desc;

            auto *cb = new QCheckBox(checkLabel);
            cb->setChecked(true);
            cb->setProperty("checkName", name);
            cb->setProperty("checkCategory", cat);
            QString tip = desc;
            if (fixable)
                tip += " (auto-fixable)";
            cb->setToolTip(tip);
            catLayout->addWidget(cb);
            m_individualChecks[name] = cb;
        }

        // Category unchecked → show individual checks
        connect(catCheck, &QCheckBox::toggled, this, [catContent, catCheck, cat, this](bool allOn) {
            catContent->setVisible(!allOn);
            if (allOn) {
                for (auto it = m_individualChecks.begin(); it != m_individualChecks.end(); ++it) {
                    if (it.value()->property("checkCategory").toString() == cat)
                        it.value()->setChecked(true);
                }
            }
        });
    }
}

QStringList MainWindow::selectedCheckNames() const
{
    QStringList selected;
    for (auto it = m_individualChecks.constBegin(); it != m_individualChecks.constEnd(); ++it) {
        QString cat = it.value()->property("checkCategory").toString();
        auto catIt = m_categoryChecks.find(cat);
        if (catIt != m_categoryChecks.end() && catIt.value()->isChecked()) {
            selected << it.key();
        } else if (it.value()->isChecked()) {
            selected << it.key();
        }
    }
    return selected;
}

int MainWindow::totalCheckCount() const
{
    return m_individualChecks.size();
}

void MainWindow::onQuitTriggered()
{
    if (m_bCleanRunning) m_pCleanProcess->kill();
    QApplication::quit();
}

// ---------------------------------------------------------------------------
// Directory / file listing
// ---------------------------------------------------------------------------
void MainWindow::onUpdateInDir(const QString &newInDir)
{
    m_dirWatcherTimer->stop();
    if (!m_sInDir.isEmpty()) m_fsWatcher.removePath(m_sInDir);
    m_bFilesHaveChanged = false;
    m_fsWatcher.addPath(newInDir);
    m_inDirectory->setText(newInDir);
    m_inDirectory->setStyleSheet("");
    m_sInDir = newInDir;
    m_cleanButton->setEnabled(true);
    updateFileListing();
    m_dirWatcherTimer->start();
    populateRefModelCombo();
}

void MainWindow::populateRefModelCombo()
{
    QString current = m_refModelCombo->currentData().toString();
    m_refModelCombo->clear();
    m_refModelCombo->addItem("(none)");
    if (!m_sInDir.isEmpty()) {
        QDir dir(m_sInDir);
        dir.setNameFilters({"*.mdl"});
        dir.setFilter(QDir::Files | QDir::Readable);
        for (const QString &f : dir.entryList())
            m_refModelCombo->addItem(f, dir.absoluteFilePath(f));
    }
    if (!current.isEmpty()) {
        int idx = m_refModelCombo->findData(current);
        if (idx >= 0) m_refModelCombo->setCurrentIndex(idx);
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
            m_bUpdateFilesAfterClean = true;
        else
            updateFileListing();
    }
}

QString MainWindow::humanFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
}

void MainWindow::updateFileListing()
{
    m_filesTable->setRowCount(0);
    QDir dir(m_inDirectory->text());
    dir.setNameFilters(QStringList(m_filePattern->text()));
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable | QDir::CaseSensitive);
    QStringList totalfiles = dir.entryList();
    m_mdlsDetectedLabel->setText("Detected: " + QString::number(totalfiles.count()));
    m_mdlsCleanedLabel->setText(
        m_radioCompile->isChecked() ? "Compiled: 0" :
        m_radioDecompile->isChecked() ? "Decompiled: 0" : "Cleaned: 0");
    m_mdlsFailedLabel->setText("Failed: 0");

    for (const QString &filePath : totalfiles)
    {
        QString fullPath = m_inDirectory->text() + "/" + filePath;
        QFile inputFile(fullPath);
        if (!inputFile.open(QIODevice::ReadOnly)) continue;
        inputFile.close();
        bool isASCII = !ModelViewport::fileIsBinaryMdl(fullPath);
        auto *fileNameItem = new QTableWidgetItem();
        fileNameItem->setText((isASCII ? QString::fromUtf8("📄 ") : QString::fromUtf8("📦 ")) + filePath);
        fileNameItem->setData(Qt::UserRole, filePath);
        fileNameItem->setToolTip(isASCII ? "ASCII MDL" : "Binary MDL");
        auto *fileSizeItem = new QTableWidgetItem(humanFileSize(inputFile.size()));
        fileSizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fileSizeItem->setData(Qt::UserRole, inputFile.size());
        auto *statusItem = new QTableWidgetItem("Pending");
        statusItem->setTextAlignment(Qt::AlignCenter);
        auto *fixesItem = new QTableWidgetItem();
        fixesItem->setTextAlignment(Qt::AlignCenter);
        auto *timerItem = new QTableWidgetItem();
        timerItem->setTextAlignment(Qt::AlignCenter);
        int row = m_filesTable->rowCount();
        m_filesTable->insertRow(row);
        m_filesTable->setItem(row, 0, fileNameItem);
        m_filesTable->setItem(row, 1, fileSizeItem);
        m_filesTable->setItem(row, 2, statusItem);
        m_filesTable->setItem(row, 3, fixesItem);
        m_filesTable->setItem(row, 4, timerItem);
    }
}

void MainWindow::copyToClipboard()
{
    if (m_filesTable->selectedItems().isEmpty()) return;
    auto *selectedFile = m_filesTable->item(m_filesTable->currentRow(), 0);
    QApplication::clipboard()->setText(m_sInDir + "/" + selectedFile->data(Qt::UserRole).toString());
}

// ---------------------------------------------------------------------------
// Sidebar / raw-log / detail panel
// ---------------------------------------------------------------------------
void MainWindow::toggleSidebar()
{
    bool visible = m_sidebarWidget->isVisible();
    m_sidebarWidget->setVisible(!visible);
    m_sidebarToggleBtn->setText(visible ? tr("Show Sidebar") : tr("Hide Sidebar"));
    m_sidebarToggleBtn->setVisible(visible);
}

void MainWindow::toggleRawLog()
{
    m_rawLogVisible = !m_rawLogVisible;
    m_rawLogDrawer->setVisible(m_rawLogVisible);
}

void MainWindow::showFileDetails(const QString &fileName)
{
    if (m_fileResults.contains(fileName)) {
        m_detailPanel->setHtml(m_fileResults[fileName].join(""));
    } else {
        m_detailPanel->setHtml(LogHtml::detailLine(LogColor::Placeholder,
                                                   QStringLiteral("No results yet for this file.")));
    }
}

void MainWindow::showBatchSummary()
{
    if (m_batchSummary.isEmpty()) {
        m_detailPanel->setHtml("");
        return;
    }
    m_detailPanel->setHtml("<p><b>" + m_batchSummary.toHtmlEscaped() + "</b></p>");
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (QFileInfo(path).isDir()) {
            onUpdateInDir(path);
            return;
        }
    }
}
