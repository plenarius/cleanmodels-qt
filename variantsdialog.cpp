#include "variantsdialog.h"
#include "constants.h"
#include "modelviewport.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QMap>
#include <QSet>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <utility>

namespace {

// The adjustments a tile can have, in the adjust panel's order: the CLI's
// --width, --length and the --shape regions.
struct AdjustParam { const char *name; const char *label; const char *tip; };
constexpr AdjustParam kAdjustParams[] = {
    {"width", QT_TR_NOOP("Width"), QT_TR_NOOP("Width across every bone, against the fit: above 100 % is looser, below is tighter")},
    {"length", QT_TR_NOOP("Length"), QT_TR_NOOP("Stretch along every bone, against the fit")},
    {"shoulders", QT_TR_NOOP("Shoulders"), QT_TR_NOOP("Width at the shoulders (sleeves stay on the arms)")},
    {"chest", QT_TR_NOOP("Chest"), QT_TR_NOOP("Width halfway between the shoulders and the waist")},
    {"waist", QT_TR_NOOP("Waist"), QT_TR_NOOP("Width at the waist (torso_g)")},
    {"hips", QT_TR_NOOP("Hips"), QT_TR_NOOP("Width at the hips (the thigh joints)")},
    {"hem", QT_TR_NOOP("Hem"), QT_TR_NOOP("Width at the lowest edge")},
};

// The tiles' starting view: from the front (creatures face +y), a little to
// the side and above.
constexpr float kStartYaw = 1.1f;
constexpr float kStartPitch = 0.25f;

// A body prefix (pmh0, pmh15) from a robe or part file name
// (pmh0_robe003.mdl), or "" when the name does not start with one.
QString bodyPrefix(const QString &path)
{
    static const QRegularExpression re(QStringLiteral("^(p[mf][a-z][0-9]+)_"));
    const auto m = re.match(QFileInfo(path).completeBaseName().toLower());
    return m.hasMatch() ? m.captured(1) : QString();
}

// "3, 5, 9, 15-21" from sorted phenotype numbers.
QString phenotypeRanges(QList<int> ns)
{
    std::sort(ns.begin(), ns.end());
    QStringList parts;
    for (int i = 0; i < ns.size();) {
        int j = i;
        while (j + 1 < ns.size() && ns[j + 1] == ns[j] + 1)
            ++j;
        parts << (j > i + 1 ? QStringLiteral("%1-%2").arg(ns[i]).arg(ns[j])
                            : j == i + 1 ? QStringLiteral("%1, %2").arg(ns[i]).arg(ns[j])
                                         : QString::number(ns[i]));
        i = j + 1;
    }
    return parts.join(QStringLiteral(", "));
}

// The CLI's message for an existing output, in the dialog's terms.
QString friendlyError(const QString &msg)
{
    if (msg.contains(QStringLiteral("exists; add --overwrite")))
        return QObject::tr("The file already exists. Tick \"Replace existing files\", or use Remake.");
    return msg;
}

} // namespace

VariantsDialog::VariantsDialog(const QString &cliPath, const QStringList &lookupArgs, QWidget *parent)
    : QDialog(parent), m_cliPath(cliPath), m_lookupArgs(lookupArgs)
{
    setWindowTitle(tr("Make Variants"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setSizeGripEnabled(true);
    resize(1200, 820);
    buildUi();
    rebuildGrid();
    updateControls();
}

VariantsDialog::~VariantsDialog()
{
    if (m_listProcess) {
        m_listProcess->disconnect(this);
        m_listProcess->kill();
        m_listProcess->waitForFinished(2000);
    }
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

void VariantsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // --- Source, output and options ---
    auto *form = new QGridLayout;
    form->setHorizontalSpacing(6);
    form->setVerticalSpacing(4);

    m_sourceEdit = new QLineEdit;
    m_sourceEdit->setPlaceholderText(tr("A robe, cloak or armour part named for its body: pmh0_robe003.mdl, pmh0_chest050.mdl"));
    auto *sourceBrowse = new QPushButton(tr("Browse..."));
    form->addWidget(new QLabel(tr("Model:")), 0, 0);
    form->addWidget(m_sourceEdit, 0, 1);
    form->addWidget(sourceBrowse, 0, 2);

    m_outDirEdit = new QLineEdit;
    m_outDirEdit->setPlaceholderText(tr("The model's folder"));
    auto *outBrowse = new QPushButton(tr("Browse..."));
    form->addWidget(new QLabel(tr("Output folder:")), 1, 0);
    form->addWidget(m_outDirEdit, 1, 1);
    form->addWidget(outBrowse, 1, 2);
    root->addLayout(form);

    auto *optRow = new QHBoxLayout;
    m_compileCheck = new QCheckBox(tr("Compile (binary)"));
    m_compileCheck->setToolTip(tr("Write compiled models instead of ASCII"));
    m_overwriteCheck = new QCheckBox(tr("Replace existing files"));
    m_overwriteCheck->setToolTip(tr("Without this, a variant whose file already exists in the output folder is not written"));
    m_syncViewsCheck = new QCheckBox(tr("Turn views together"));
    m_syncViewsCheck->setChecked(true);
    m_syncViewsCheck->setToolTip(tr("Orbiting one view turns all of them"));
    m_makeButton = new QPushButton(tr("Make Variants"));
    m_makeButton->setDefault(true);
    optRow->addWidget(m_compileCheck);
    optRow->addWidget(m_overwriteCheck);
    optRow->addWidget(m_syncViewsCheck);
    optRow->addStretch(1);
    optRow->addWidget(m_makeButton);
    root->addLayout(optRow);

    // --- Grid ---
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    m_gridHost = new QWidget;
    auto *hostLayout = new QVBoxLayout(m_gridHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    m_gridNote = new QLabel;
    m_gridNote->setAlignment(Qt::AlignCenter);
    m_gridNote->setWordWrap(true);
    hostLayout->addWidget(m_gridNote);
    m_grid = new QGridLayout;
    m_grid->setSpacing(6);
    hostLayout->addLayout(m_grid, 1);
    scroll->setWidget(m_gridHost);
    root->addWidget(scroll, 1);

    // --- Adjust panel for the selected tile ---
    m_remakeTimer = new QTimer(this);
    m_remakeTimer->setSingleShot(true);
    m_remakeTimer->setInterval(300);
    connect(m_remakeTimer, &QTimer::timeout, this, &VariantsDialog::scheduleRemake);
    m_adjustBox = new QWidget;
    auto *adjust = new QGridLayout(m_adjustBox);
    adjust->setContentsMargins(0, 0, 0, 0);
    adjust->setHorizontalSpacing(6);
    adjust->setVerticalSpacing(2);
    m_adjustTitle = new QLabel;
    adjust->addWidget(m_adjustTitle, 0, 0, 1, 6);
    int idx = 0;
    for (const AdjustParam &a : kAdjustParams) {
        const QString name = QString::fromLatin1(a.name);
        auto *label = new QLabel(tr(a.label));
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(50, 200);
        slider->setValue(100);
        slider->setToolTip(tr(a.tip));
        auto *spin = new QSpinBox;
        spin->setRange(50, 200);
        spin->setValue(100);
        spin->setSuffix(QStringLiteral(" %"));
        spin->setToolTip(tr(a.tip));
        const int row = 1 + idx / 3, col = (idx % 3) * 3;
        adjust->addWidget(label, row, col);
        adjust->addWidget(slider, row, col + 1);
        adjust->addWidget(spin, row, col + 2);
        m_sliders[name] = slider;
        m_spins[name] = spin;
        // The slider and the box show the same value; letting go of the
        // slider, or leaving the box, remakes the tile.
        connect(slider, &QSlider::valueChanged, this, [this, name, spin, slider](int v) {
            QSignalBlocker b(spin);
            spin->setValue(v);
            setAdjust(name, v);
            if (!slider->isSliderDown())
                m_remakeTimer->start(); // a click on the groove, the wheel, the keys
        });
        connect(slider, &QSlider::sliderReleased, this, [this] { m_remakeTimer->start(); });
        connect(spin, &QSpinBox::editingFinished, this, [this, name, spin, slider] {
            if (m_selected < 0 || m_tiles[m_selected].isSource)
                return;
            const int before = m_tiles[m_selected].adjust.value(name, 100);
            QSignalBlocker b(slider);
            slider->setValue(spin->value());
            setAdjust(name, spin->value());
            if (spin->value() != before)
                scheduleRemake(); // editingFinished also fires on leaving the box
        });
        ++idx;
    }
    for (int c = 0; c < 3; ++c)
        adjust->setColumnStretch(c * 3 + 1, 1);
    m_resetButton = new QPushButton(tr("Reset"));
    m_resetButton->setToolTip(tr("Back to the fit: every adjustment 100 %, and remake"));
    m_remakeButton = new QPushButton(tr("Remake"));
    m_remakeButton->setToolTip(tr("Make this tile's bodies again with these adjustments, replacing their files"));
    m_openButton = new QPushButton(tr("Open in Viewport"));
    m_openButton->setToolTip(tr("Show the written file in the main window's viewport"));
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_resetButton);
    buttons->addWidget(m_remakeButton);
    buttons->addWidget(m_openButton);
    adjust->addLayout(buttons, 1 + (idx + 2) / 3, 0, 1, 9);
    root->addWidget(m_adjustBox);

    // --- Status ---
    auto *statusRow = new QHBoxLayout;
    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    m_progress->setMaximumWidth(200);
    statusRow->addWidget(m_statusLabel, 1);
    statusRow->addWidget(m_progress);
    root->addLayout(statusRow);

    connect(sourceBrowse, &QPushButton::clicked, this, [this] {
        const QString start = m_sourceEdit->text().isEmpty() ? QString() : QFileInfo(m_sourceEdit->text()).absolutePath();
        const QString f = QFileDialog::getOpenFileName(this, tr("Model to make variants of"), start, tr("Models (*.mdl)"));
        if (!f.isEmpty())
            setSource(f);
    });
    connect(m_sourceEdit, &QLineEdit::editingFinished, this, [this] {
        // Also fires when the field merely loses focus; only a new model
        // starts a new grid.
        if (m_sourceEdit->text().trimmed() == m_gridSource)
            return;
        rebuildGrid();
        updateControls();
    });
    connect(outBrowse, &QPushButton::clicked, this, [this] {
        QString start = m_outDirEdit->text();
        if (start.isEmpty() && !m_sourceEdit->text().isEmpty())
            start = QFileInfo(m_sourceEdit->text()).absolutePath();
        const QString d = QFileDialog::getExistingDirectory(this, tr("Output folder"), start);
        if (!d.isEmpty())
            m_outDirEdit->setText(d);
    });
    connect(m_makeButton, &QPushButton::clicked, this, [this] {
        if (m_process) {
            m_stopping = true;
            m_jobs.clear();
            m_pendingRemakes.clear();
            m_process->kill();
            return;
        }
        makeVariants();
    });
    connect(m_resetButton, &QPushButton::clicked, this, [this] {
        if (m_selected < 0 || m_tiles[m_selected].isSource)
            return;
        m_tiles[m_selected].adjust.clear();
        refreshAdjustBar();
        refreshTileStatus(m_tiles[m_selected]);
        scheduleRemake();
    });
    connect(m_remakeButton, &QPushButton::clicked, this, &VariantsDialog::remakeSelected);
    connect(m_openButton, &QPushButton::clicked, this, [this] {
        if (m_selected >= 0 && !m_tiles[m_selected].output.isEmpty())
            emit openInViewport(m_tiles[m_selected].output);
    });
}

void VariantsDialog::setSource(const QString &mdlPath)
{
    if (m_process)
        return;
    m_sourceEdit->setText(QDir::toNativeSeparators(mdlPath));
    rebuildGrid();
    updateControls();
}

void VariantsDialog::setLookupArgs(const QStringList &args)
{
    if (args == m_lookupArgs)
        return;
    m_lookupArgs = args;
    if (!m_process && !m_gridSource.isEmpty())
        rebuildGrid();
}

QString VariantsDialog::sourcePrefix() const
{
    return bodyPrefix(m_sourceEdit->text().trimmed());
}

void VariantsDialog::clearGrid()
{
    for (Tile &t : m_tiles) {
        t.frame->removeEventFilter(this);
        t.view->removeEventFilter(this);
        delete t.frame;
    }
    m_tiles.clear();
    m_tileOf.clear();
    m_pendingRemakes.clear();
    m_selected = -1;
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

void VariantsDialog::rebuildGrid()
{
    clearGrid();
    if (m_listProcess) {
        m_listProcess->disconnect(this);
        m_listProcess->kill();
        m_listProcess->deleteLater();
        m_listProcess = nullptr;
    }

    const QString source = m_sourceEdit->text().trimmed();
    const QString prefix = sourcePrefix();
    m_gridSource = source;
    m_gridNote->setVisible(true);
    if (source.isEmpty() || prefix.isEmpty()) {
        m_gridNote->setText(source.isEmpty()
            ? tr("Pick a robe, cloak or armour part to make its versions for the other races and phenotypes.")
            : tr("%1 is not named for a body. Variants are made from models such as pmh0_robe003, pmh0_cloak_001 or pmh0_chest050.")
                  .arg(QFileInfo(source).fileName()));
        refreshAdjustBar();
        return;
    }
    if (m_cliPath.isEmpty()) {
        m_gridNote->setText(tr("cleanmodels was not found, so variants cannot be made."));
        return;
    }

    // Ask the CLI which bodies the model gets variants for: every race and
    // phenotype found in the game install and the resource folders (custom
    // races included), grouped by shape.
    m_gridNote->setText(tr("Finding the bodies..."));
    m_listProcess = new QProcess(this);
    QProcess *proc = m_listProcess;
    connect(proc, &QProcess::finished, this, [this, proc](int exitCode, QProcess::ExitStatus status) {
        if (proc != m_listProcess)
            return;
        m_listProcess = nullptr;
        proc->deleteLater();
        const QByteArray out = proc->readAllStandardOutput();
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(out, &perr);
        if (status != QProcess::NormalExit || exitCode != CliExit::Ok || !doc.isArray()) {
            QString msg = err.isEmpty() ? tr("cleanmodels could not list the bodies.") : err;
            if (err.contains(QStringLiteral("flag provided but not defined")))
                msg = tr("This cleanmodels is too old to make variants here; it needs 4.7 or later.") + QStringLiteral("\n") + err;
            m_gridNote->setText(msg);
            return;
        }
        buildTiles(doc.array());
        updateControls();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && proc == m_listProcess) {
            m_listProcess = nullptr;
            proc->deleteLater();
            m_gridNote->setText(tr("cleanmodels could not be started: %1").arg(proc->errorString()));
        }
    });
    proc->start(m_cliPath, QStringList{CliCommand::Variants, CliFlag::ListBodies, CliFlag::Json}
                               << m_lookupArgs << QDir::fromNativeSeparators(source),
                QIODevice::ReadOnly);
    refreshAdjustBar();
}

void VariantsDialog::buildTiles(const QJsonArray &bodies)
{
    clearGrid();
    const QString source = m_sourceEdit->text().trimmed();
    const QString prefix = sourcePrefix();

    // Shapes: a body with sameShape (in this list) is drawn by that body's
    // tile; it gets the same fit, renamed.
    struct Info { QString prefix, race, phenoName, sameShape; int pheno = 0; bool source = false; };
    QList<Info> infos;
    QSet<QString> listed;
    for (const QJsonValue &v : bodies) {
        const QJsonObject o = v.toObject();
        Info i;
        i.prefix = o.value(QStringLiteral("prefix")).toString();
        i.race = o.value(QStringLiteral("raceName")).toString();
        i.pheno = o.value(QStringLiteral("phenotype")).toInt();
        i.phenoName = o.value(QStringLiteral("phenotypeName")).toString();
        i.sameShape = o.value(QStringLiteral("sameShape")).toString();
        i.source = o.value(QStringLiteral("source")).toBool() || i.prefix == prefix;
        infos << i;
        listed << i.prefix;
    }
    if (infos.size() <= 1) {
        m_gridNote->setText(tr("No other bodies were found. Set the game folder (and your haks, for custom races) under Game Resources in the main window."));
        return;
    }
    m_gridNote->setVisible(false);

    QStringList races;              // columns, in the CLI's order
    QList<int> phenos;              // rows
    QMap<QString, QString> phenoNames;
    for (const Info &i : infos) {
        const bool rep = i.sameShape.isEmpty() || !listed.contains(i.sameShape);
        if (!races.contains(i.race))
            races << i.race;
        if (rep && !phenos.contains(i.pheno)) {
            phenos << i.pheno;
            phenoNames[QString::number(i.pheno)] = i.phenoName;
        }
    }
    std::sort(phenos.begin(), phenos.end());

    for (int c = 0; c < races.size(); ++c) {
        auto *head = new QLabel(races[c]);
        head->setAlignment(Qt::AlignCenter);
        QFont f = head->font();
        f.setBold(true);
        head->setFont(f);
        m_grid->addWidget(head, 0, c + 1);
    }
    for (int r = 0; r < phenos.size(); ++r) {
        const QString name = phenoNames.value(QString::number(phenos[r]));
        auto *head = new QLabel(name.isEmpty() ? tr("Phenotype %1").arg(phenos[r])
                                               : tr("Phenotype %1\n(%2)").arg(phenos[r]).arg(name));
        head->setAlignment(Qt::AlignCenter);
        m_grid->addWidget(head, r + 1, 0);
    }

    for (const Info &i : infos) {
        if (!i.sameShape.isEmpty() && listed.contains(i.sameShape))
            continue;
        Tile t;
        t.target = i.prefix;
        t.title = i.phenoName.isEmpty() ? tr("%1, phenotype %2").arg(i.race).arg(i.pheno)
                                        : tr("%1, %2").arg(i.race, i.phenoName);
        t.isSource = i.source;
        for (const Info &m : infos)
            if (m.sameShape == i.prefix && !m.source) {
                t.members << m.prefix;
                t.memberPhenos << m.pheno;
            }

        t.frame = new QFrame;
        t.frame->setFrameShape(QFrame::StyledPanel);
        auto *v = new QVBoxLayout(t.frame);
        v->setContentsMargins(4, 4, 4, 4);
        v->setSpacing(2);

        auto *top = new QHBoxLayout;
        if (!t.isSource) {
            t.check = new QCheckBox;
            t.check->setChecked(true);
            t.check->setToolTip(tr("Make this variant"));
            top->addWidget(t.check);
        }
        t.titleLabel = new QLabel(t.isSource ? tr("%1 (source)").arg(t.target) : t.target);
        top->addWidget(t.titleLabel, 1);
        v->addLayout(top);
        if (!t.members.isEmpty()) {
            auto *also = new QLabel(t.memberPhenos.size() == 1
                ? tr("also phenotype %1").arg(t.memberPhenos.first())
                : tr("also phenotypes %1").arg(phenotypeRanges(t.memberPhenos)));
            also->setWordWrap(true);
            also->setToolTip(t.isSource
                ? tr("Same shape as the source: these get copies of it, renamed (%1)").arg(t.members.join(QStringLiteral(", ")))
                : tr("Same shape: these get this fit, renamed (%1)").arg(t.members.join(QStringLiteral(", "))));
            v->addWidget(also);
        }

        t.view = new ModelViewport;
        t.view->setMinimumSize(160, 220);
        t.view->setCliBinaryPath(m_cliPath);
        t.view->setEmptyHint(t.isSource ? tr("Loading...") : tr("Not made yet"));
        t.view->setViewAngles(kStartYaw, kStartPitch);
        t.view->setSolidReference(true);
        t.view->setAccessibleName(t.title);
        v->addWidget(t.view, 1);

        t.status = new QLabel;
        t.status->setWordWrap(true);
        v->addWidget(t.status);

        t.frame->installEventFilter(this);
        t.view->installEventFilter(this);
        ModelViewport *view = t.view;
        connect(view, &ModelViewport::viewRotated, this, [this, view](float yaw, float pitch) {
            if (!m_syncViewsCheck->isChecked())
                return;
            for (Tile &o : m_tiles)
                if (o.view != view)
                    o.view->setViewAngles(yaw, pitch);
        });

        m_grid->addWidget(t.frame, phenos.indexOf(i.pheno) + 1, races.indexOf(i.race) + 1);
        m_tileOf[t.target] = m_tiles.size();
        for (const QString &m : t.members)
            m_tileOf[m] = m_tiles.size();
        m_tiles.append(t);
    }
    for (int c = 0; c < races.size(); ++c)
        m_grid->setColumnStretch(c + 1, 1);
    for (int r = 0; r < phenos.size(); ++r)
        m_grid->setRowStretch(r + 1, 1);

    // Until a run shows it on its body, the source tile shows the file as is.
    const int si = tileIndex(prefix);
    if (si >= 0)
        m_tiles[si].view->previewFile(QDir::fromNativeSeparators(source));

    for (Tile &t : m_tiles)
        refreshTileStatus(t);
    refreshAdjustBar();
}

int VariantsDialog::tileIndex(const QString &target) const
{
    return m_tileOf.value(target, -1);
}

void VariantsDialog::refreshTileStatus(Tile &t)
{
    QString text;
    QString tip;
    if (!t.error.isEmpty()) {
        text = QStringLiteral("<span style=\"color:%1\">%2</span>")
                   .arg(QLatin1String(LogColor::Error), tr("Not written"));
        tip = t.error;
    } else if (!t.output.isEmpty()) {
        text = QFileInfo(t.output).fileName().toHtmlEscaped();
        if (t.written > 1)
            text += QStringLiteral(" ") + tr("(%1 files)").arg(t.written);
        if (!t.warnings.isEmpty()) {
            text += QStringLiteral(" <span style=\"color:%1\">%2</span>")
                        .arg(QLatin1String(LogColor::SevWarning),
                             t.warnings.size() == 1 ? tr("1 note") : tr("%1 notes").arg(t.warnings.size()));
            tip = t.warnings.join('\n');
        }
    } else if (t.isSource) {
        if (t.written > 0)
            text = t.written == 1 ? tr("1 copy written") : tr("%1 copies written").arg(t.written);
        if (!t.warnings.isEmpty())
            tip = t.warnings.join('\n');
    }
    if (!t.isSource) {
        QStringList adj;
        for (const AdjustParam &a : kAdjustParams) {
            const int v = t.adjust.value(QString::fromLatin1(a.name), 100);
            if (v != 100)
                adj << tr("%1 %2 %").arg(tr(a.label).toLower()).arg(v);
        }
        if (!adj.isEmpty())
            text += QStringLiteral("<br>") + adj.join(QStringLiteral(", "));
    }
    t.status->setText(text);
    t.status->setToolTip(tip);
    t.view->setToolTip(tip);
}

void VariantsDialog::selectTile(int index)
{
    // A change still waiting to be remade belongs to the tile being left.
    if (m_remakeTimer->isActive()) {
        m_remakeTimer->stop();
        scheduleRemake();
    }
    m_selected = index;
    for (int i = 0; i < m_tiles.size(); ++i) {
        m_tiles[i].frame->setFrameShape(i == index ? QFrame::Box : QFrame::StyledPanel);
        m_tiles[i].frame->setLineWidth(i == index ? 2 : 1);
    }
    refreshAdjustBar();
}

void VariantsDialog::refreshAdjustBar()
{
    const bool running = m_process != nullptr;
    const Tile *t = (m_selected >= 0 && m_selected < m_tiles.size()) ? &m_tiles[m_selected] : nullptr;
    const bool variant = t && !t->isSource;
    if (!t)
        m_adjustTitle->setText(tr("Click a tile to adjust that variant."));
    else if (t->isSource)
        m_adjustTitle->setText(tr("%1 (%2): the source").arg(t->title, t->target));
    else
        m_adjustTitle->setText(tr("%1 (%2)").arg(t->title, t->target));
    for (auto it = m_sliders.begin(); it != m_sliders.end(); ++it) {
        const int v = variant ? t->adjust.value(it.key(), 100) : 100;
        QSignalBlocker bs(it.value()), bb(m_spins[it.key()]);
        it.value()->setValue(v);
        m_spins[it.key()]->setValue(v);
        it.value()->setEnabled(variant);
        m_spins[it.key()]->setEnabled(variant);
    }
    m_resetButton->setEnabled(variant && !t->adjust.isEmpty());
    m_remakeButton->setEnabled(variant && !running);
    m_openButton->setEnabled(t && !t->output.isEmpty());
}

void VariantsDialog::updateControls()
{
    const bool running = m_process != nullptr;
    const bool ready = !m_tiles.isEmpty() && !m_cliPath.isEmpty();
    m_makeButton->setText(running ? tr("Stop") : tr("Make Variants"));
    m_makeButton->setEnabled(running || ready);
    m_sourceEdit->setEnabled(!running);
    m_outDirEdit->setEnabled(!running);
    m_compileCheck->setEnabled(!running);
    m_overwriteCheck->setEnabled(!running);
    for (Tile &t : m_tiles)
        if (t.check)
            t.check->setEnabled(!running);
    refreshAdjustBar();
    if (m_cliPath.isEmpty())
        m_statusLabel->setText(tr("cleanmodels was not found, so variants cannot be made."));
}

bool VariantsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (watched != m_tiles[i].frame && watched != m_tiles[i].view)
                continue;
            if (m_selected != i)
                selectTile(i);
            if (event->type() == QEvent::MouseButtonDblClick && !m_tiles[i].output.isEmpty())
                emit openInViewport(m_tiles[i].output);
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void VariantsDialog::closeEvent(QCloseEvent *event)
{
    if (m_process) {
        m_stopping = true;
        m_jobs.clear();
        m_pendingRemakes.clear();
        m_process->kill();
    }
    QDialog::closeEvent(event);
}

void VariantsDialog::makeVariants()
{
    if (m_tiles.isEmpty())
        return;
    // One run per set of adjustments, so adjusted tiles keep theirs.
    m_jobs.clear();
    for (const Tile &t : m_tiles) {
        if (t.isSource) {
            // Bodies of the source's shape get copies of it, renamed.
            if (!t.members.isEmpty()) {
                auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [](const Job &j) {
                    return j.adjust.isEmpty();
                });
                if (it == m_jobs.end()) {
                    m_jobs.append(Job{{}, {}, m_overwriteCheck->isChecked()});
                    it = m_jobs.end() - 1;
                }
                it->targets << t.members;
            }
            continue;
        }
        if (!t.check || !t.check->isChecked())
            continue;
        auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const Job &j) {
            return j.adjust == t.adjust;
        });
        if (it == m_jobs.end()) {
            m_jobs.append(Job{{}, t.adjust, m_overwriteCheck->isChecked()});
            it = m_jobs.end() - 1;
        }
        it->targets << t.target << t.members;
    }
    if (m_jobs.isEmpty()) {
        m_statusLabel->setText(tr("Tick at least one body."));
        return;
    }
    m_jobTotal = 0;
    for (const Job &j : m_jobs)
        m_jobTotal += j.targets.size();
    m_jobDone = m_written = m_failed = 0;
    m_progress->setRange(0, m_jobTotal);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    startNextJob();
}

void VariantsDialog::remakeSelected()
{
    remakeTile(m_selected);
}

void VariantsDialog::remakeTile(int index)
{
    if (m_process || index < 0 || index >= m_tiles.size() || m_tiles[index].isSource)
        return;
    const Tile &t = m_tiles[index];
    m_jobs = {Job{QStringList{t.target} << t.members, t.adjust, true}};
    m_jobTotal = m_jobs.first().targets.size();
    m_jobDone = m_written = m_failed = 0;
    m_progress->setRange(0, m_jobTotal);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    startNextJob();
}

// Remakes the selected tile now, or when the run going on ends.
void VariantsDialog::scheduleRemake()
{
    if (m_selected < 0 || m_tiles[m_selected].isSource)
        return;
    if (m_process) {
        if (!m_pendingRemakes.contains(m_selected))
            m_pendingRemakes << m_selected;
        return;
    }
    remakeSelected();
}

void VariantsDialog::setAdjust(const QString &name, int percent)
{
    if (m_selected < 0 || m_tiles[m_selected].isSource)
        return;
    Tile &t = m_tiles[m_selected];
    if (percent == 100)
        t.adjust.remove(name);
    else
        t.adjust[name] = percent;
    m_resetButton->setEnabled(!t.adjust.isEmpty());
}

void VariantsDialog::startNextJob()
{
    if (m_jobs.isEmpty()) {
        m_progress->setVisible(false);
        QString msg = tr("%1 of %2 variants written.").arg(m_written).arg(m_jobTotal);
        if (m_failed > 0)
            msg += QLatin1Char(' ') + tr("Point at a tile marked \"Not written\" to see why.");
        else if (m_written > 0)
            msg += QLatin1Char(' ') + tr("Blue is each body's base parts: where it shows through, the variant is too tight there.");
        m_statusLabel->setText(msg);
        updateControls();
        if (!m_pendingRemakes.isEmpty())
            remakeTile(m_pendingRemakes.takeFirst());
        return;
    }
    if (!m_previewDir.isValid()) {
        m_jobs.clear();
        m_statusLabel->setText(tr("Cannot make a temporary folder for the previews."));
        updateControls();
        return;
    }
    const Job job = m_jobs.takeFirst();
    const QString source = QDir::fromNativeSeparators(m_sourceEdit->text().trimmed());
    QString outDir = QDir::fromNativeSeparators(m_outDirEdit->text().trimmed());
    if (outDir.isEmpty())
        outDir = QFileInfo(source).absolutePath();

    auto factor = [&job](const char *name) {
        return QString::number(job.adjust.value(QString::fromLatin1(name), 100) / 100.0, 'g', 4);
    };
    QStringList args{CliCommand::Variants, CliFlag::JsonLines,
                     CliFlag::To, job.targets.join(','),
                     CliFlag::Width, factor("width"),
                     CliFlag::Length, factor("length"),
                     CliFlag::PreviewDir, m_previewDir.path()};
    QStringList shape;
    for (const AdjustParam &a : kAdjustParams) {
        const QString name = QString::fromLatin1(a.name);
        if (name != QLatin1String("width") && name != QLatin1String("length") && job.adjust.value(name, 100) != 100)
            shape << name + QLatin1Char('=') + factor(a.name);
    }
    if (!shape.isEmpty())
        args << CliFlag::Shape << shape.join(QLatin1Char(','));
    if (m_compileCheck->isChecked())
        args << CliFlag::CompileOut;
    if (job.overwrite)
        args << CliFlag::Overwrite;
    args << m_lookupArgs << source << outDir;

    for (const QString &target : job.targets) {
        const int i = tileIndex(target);
        if (i >= 0 && m_tiles[i].target == target) {
            m_tiles[i].error.clear();
            m_tiles[i].written = 0;
            m_tiles[i].status->setText(tr("Waiting..."));
        }
    }

    m_stdout.clear();
    m_stderr.clear();
    m_stopping = false;
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &VariantsDialog::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process->readAllStandardError();
    });
    connect(m_process, &QProcess::finished, this, &VariantsDialog::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart)
            onFinished(-1, QProcess::CrashExit);
    });
    m_process->start(m_cliPath, args, QIODevice::ReadOnly);
    updateControls();
}

void VariantsDialog::onReadyRead()
{
    m_stdout += m_process->readAllStandardOutput();
    int nl;
    while ((nl = m_stdout.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdout.left(nl).trimmed();
        m_stdout.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            handleEvent(doc.object());
    }
}

void VariantsDialog::handleEvent(const QJsonObject &evt)
{
    const QString type = evt.value(QStringLiteral("type")).toString();
    const QString target = evt.value(QStringLiteral("target")).toString();
    const int i = tileIndex(target);
    if (i < 0)
        return;
    Tile &t = m_tiles[i];
    const QString preview = evt.value(QStringLiteral("preview")).toString();
    const QString body = evt.value(QStringLiteral("body")).toString();
    auto show = [&] {
        if (preview.isEmpty())
            return;
        t.view->previewFile(preview);
        if (body.isEmpty())
            t.view->clearReference();
        else
            t.view->loadReferenceFile(body);
    };

    if (type == QLatin1String("source")) {
        t.warnings.clear();
        const QString msg = evt.value(QStringLiteral("message")).toString();
        if (!msg.isEmpty())
            t.warnings = msg.split(QStringLiteral("; "));
        show();
        refreshTileStatus(t);
    } else if (type == QLatin1String("start")) {
        if (t.target == target)
            t.status->setText(tr("Making..."));
        m_statusLabel->setText(tr("Making %1...").arg(evt.value(QStringLiteral("file")).toString()));
    } else if (type == QLatin1String("done")) {
        ++t.written;
        if (t.target == target) {
            // The tile's own body: its file, notes and preview. Bodies of the
            // same shape only add to the count.
            t.output = evt.value(QStringLiteral("output")).toString();
            t.error.clear();
            t.warnings.clear();
            const QJsonArray ws = evt.value(QStringLiteral("result")).toObject().value(QStringLiteral("warnings")).toArray();
            for (const QJsonValue &w : ws)
                t.warnings << w.toObject().value(QStringLiteral("message")).toString();
            show();
        }
        refreshTileStatus(t);
        ++m_written;
        m_progress->setValue(++m_jobDone);
    } else if (type == QLatin1String("error")) {
        const QString msg = friendlyError(evt.value(QStringLiteral("message")).toString());
        t.error = t.target == target ? msg : QStringLiteral("%1: %2").arg(target, msg);
        refreshTileStatus(t);
        ++m_failed;
        m_progress->setValue(++m_jobDone);
    }
    if (i == m_selected)
        refreshAdjustBar();
}

void VariantsDialog::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;
    onReadyRead();
    m_stderr += m_process->readAllStandardError();
    const QString err = QString::fromUtf8(m_stderr).trimmed();
    const bool stopped = std::exchange(m_stopping, false);
    m_process->deleteLater();
    m_process = nullptr;

    if (exitCode == CliExit::Usage || (status == QProcess::CrashExit && !stopped)) {
        // Nothing was made: a usage error (an older cleanmodels without these
        // flags) or a crash. Say so and drop the rest of the queue.
        m_jobs.clear();
        m_progress->setVisible(false);
        QString msg = err.isEmpty() ? tr("cleanmodels stopped unexpectedly.") : err;
        if (err.contains(QStringLiteral("flag provided but not defined")) || err.contains(QStringLiteral("unknown command")))
            msg = tr("This cleanmodels is too old to make variants here; it needs 4.7 or later.") + QStringLiteral("\n") + err;
        m_statusLabel->setText(msg);
        updateControls();
        return;
    }
    if (stopped) {
        m_progress->setVisible(false);
        m_statusLabel->setText(tr("Stopped."));
        for (Tile &t : m_tiles)
            refreshTileStatus(t);
        updateControls();
        return;
    }
    if (!err.isEmpty() && m_written == 0 && m_failed == 0) {
        // A whole-run failure (the source does not load, no skeleton...).
        m_jobs.clear();
        m_progress->setVisible(false);
        m_statusLabel->setText(err);
        for (Tile &t : m_tiles)
            refreshTileStatus(t);
        updateControls();
        return;
    }
    startNextJob();
}
