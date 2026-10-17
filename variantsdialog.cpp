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
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <utility>

namespace {

// The races variants are made for, by the letter in the body prefix, in the
// grid's column order. Half-elves wear the human models.
struct Race { char letter; const char *name; };
constexpr Race kRaces[] = {
    {'h', "Human"}, {'d', "Dwarf"}, {'e', "Elf"},
    {'g', "Gnome"}, {'a', "Halfling"}, {'o', "Half-orc"},
};
struct Pheno { char digit; const char *name; };
constexpr Pheno kPhenos[] = {{'0', "normal"}, {'2', "large"}};

// The tiles' starting view: from the front (creatures face +y), a little to
// the side and above.
constexpr float kStartYaw = 1.1f;
constexpr float kStartPitch = 0.25f;

// A body prefix (pmh0) from a robe or part file name (pmh0_robe003.mdl), or
// "" when the name does not start with one.
QString bodyPrefix(const QString &path)
{
    const QString base = QFileInfo(path).completeBaseName().toLower();
    const int us = base.indexOf('_');
    if (us != 4 || !base.startsWith('p') || (base[1] != 'm' && base[1] != 'f'))
        return {};
    return base.left(4);
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

    // --- Adjust bar for the selected tile ---
    auto *adjust = new QHBoxLayout;
    m_adjustTitle = new QLabel;
    m_widthSpin = new QSpinBox;
    m_widthSpin->setRange(50, 200);
    m_widthSpin->setSuffix(QStringLiteral(" %"));
    m_widthSpin->setToolTip(tr("Width across every bone, against the fit: above 100 % is looser, below is tighter"));
    m_lengthSpin = new QSpinBox;
    m_lengthSpin->setRange(50, 200);
    m_lengthSpin->setSuffix(QStringLiteral(" %"));
    m_lengthSpin->setToolTip(tr("Stretch along every bone, against the fit"));
    m_remakeButton = new QPushButton(tr("Remake"));
    m_remakeButton->setToolTip(tr("Make this variant again with the width and length set here, replacing its file"));
    m_openButton = new QPushButton(tr("Open in Viewport"));
    m_openButton->setToolTip(tr("Show the written file in the main window's viewport"));
    adjust->addWidget(m_adjustTitle, 1);
    adjust->addWidget(new QLabel(tr("Width:")));
    adjust->addWidget(m_widthSpin);
    adjust->addWidget(new QLabel(tr("Length:")));
    adjust->addWidget(m_lengthSpin);
    adjust->addWidget(m_remakeButton);
    adjust->addWidget(m_openButton);
    root->addLayout(adjust);

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
            m_process->kill();
            return;
        }
        makeVariants();
    });
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_selected >= 0 && !m_tiles[m_selected].isSource)
            m_tiles[m_selected].width = v / 100.0;
    });
    connect(m_lengthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_selected >= 0 && !m_tiles[m_selected].isSource)
            m_tiles[m_selected].length = v / 100.0;
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

QString VariantsDialog::sourcePrefix() const
{
    return bodyPrefix(m_sourceEdit->text().trimmed());
}

void VariantsDialog::rebuildGrid()
{
    for (Tile &t : m_tiles) {
        t.frame->removeEventFilter(this);
        t.view->removeEventFilter(this);
        delete t.frame;
    }
    m_tiles.clear();
    m_selected = -1;
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const QString source = m_sourceEdit->text().trimmed();
    const QString prefix = sourcePrefix();
    m_gridSource = source;
    if (source.isEmpty() || prefix.isEmpty()) {
        m_gridNote->setText(source.isEmpty()
            ? tr("Pick a robe, cloak or armour part to make its versions for the other races and phenotypes.")
            : tr("%1 is not named for a body. Variants are made from models such as pmh0_robe003, pmh0_cloak_001 or pmh0_chest050.")
                  .arg(QFileInfo(source).fileName()));
        m_gridNote->setVisible(true);
        refreshAdjustBar();
        return;
    }
    m_gridNote->setVisible(false);

    const QChar gender = prefix[1];
    for (int c = 0; c < int(std::size(kRaces)); ++c) {
        auto *head = new QLabel(QString::fromLatin1(kRaces[c].name));
        head->setAlignment(Qt::AlignCenter);
        QFont f = head->font();
        f.setBold(true);
        head->setFont(f);
        m_grid->addWidget(head, 0, c + 1);
    }
    for (int r = 0; r < int(std::size(kPhenos)); ++r) {
        auto *head = new QLabel(tr("Phenotype %1\n(%2)").arg(QChar(kPhenos[r].digit)).arg(QString::fromLatin1(kPhenos[r].name)));
        head->setAlignment(Qt::AlignCenter);
        m_grid->addWidget(head, r + 1, 0);

        for (int c = 0; c < int(std::size(kRaces)); ++c) {
            Tile t;
            t.target = QStringLiteral("p") + gender + QChar(kRaces[c].letter) + QChar(kPhenos[r].digit);
            t.title = tr("%1, %2").arg(QString::fromLatin1(kRaces[c].name), QString::fromLatin1(kPhenos[r].name));
            t.isSource = t.target == prefix;

            t.frame = new QFrame;
            t.frame->setObjectName(QStringLiteral("variantTile"));
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

            m_grid->addWidget(t.frame, r + 1, c + 1);
            m_tiles.append(t);
        }
    }
    for (int c = 0; c < int(std::size(kRaces)); ++c)
        m_grid->setColumnStretch(c + 1, 1);
    for (int r = 0; r < int(std::size(kPhenos)); ++r)
        m_grid->setRowStretch(r + 1, 1);

    // Until a run shows it on its body, the source tile shows the file as is.
    const int si = tileIndex(prefix);
    if (si >= 0)
        m_tiles[si].view->previewFile(source);

    for (Tile &t : m_tiles)
        refreshTileStatus(t);
    refreshAdjustBar();
}

int VariantsDialog::tileIndex(const QString &target) const
{
    for (int i = 0; i < m_tiles.size(); ++i)
        if (m_tiles[i].target == target)
            return i;
    return -1;
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
        if (!t.warnings.isEmpty()) {
            text += QStringLiteral(" <span style=\"color:%1\">%2</span>")
                        .arg(QLatin1String(LogColor::SevWarning),
                             t.warnings.size() == 1 ? tr("1 note") : tr("%1 notes").arg(t.warnings.size()));
            tip = t.warnings.join('\n');
        }
    } else if (t.isSource && !t.warnings.isEmpty()) {
        tip = t.warnings.join('\n');
    }
    if (!t.isSource && (t.width != 1.0 || t.length != 1.0))
        text += QStringLiteral("<br>") + tr("width %1 %, length %2 %").arg(qRound(t.width * 100)).arg(qRound(t.length * 100));
    t.status->setText(text);
    t.status->setToolTip(tip);
    t.view->setToolTip(tip);
}

void VariantsDialog::selectTile(int index)
{
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
    {
        QSignalBlocker bw(m_widthSpin), bl(m_lengthSpin);
        m_widthSpin->setValue(variant ? qRound(t->width * 100) : 100);
        m_lengthSpin->setValue(variant ? qRound(t->length * 100) : 100);
    }
    m_widthSpin->setEnabled(variant);
    m_lengthSpin->setEnabled(variant);
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
        m_process->kill();
    }
    QDialog::closeEvent(event);
}

void VariantsDialog::makeVariants()
{
    if (m_tiles.isEmpty())
        return;
    // One run per width and length, so adjusted tiles keep their adjustment.
    m_jobs.clear();
    for (const Tile &t : m_tiles) {
        if (t.isSource || !t.check || !t.check->isChecked())
            continue;
        auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const Job &j) {
            return j.width == t.width && j.length == t.length;
        });
        if (it == m_jobs.end()) {
            m_jobs.append(Job{{}, t.width, t.length, m_overwriteCheck->isChecked()});
            it = m_jobs.end() - 1;
        }
        it->targets << t.target;
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
    if (m_process || m_selected < 0 || m_tiles[m_selected].isSource)
        return;
    const Tile &t = m_tiles[m_selected];
    m_jobs = {Job{{t.target}, t.width, t.length, true}};
    m_jobTotal = 1;
    m_jobDone = m_written = m_failed = 0;
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    startNextJob();
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

    QStringList args{CliCommand::Variants, CliFlag::JsonLines,
                     CliFlag::To, job.targets.join(','),
                     CliFlag::Width, QString::number(job.width, 'g', 4),
                     CliFlag::Length, QString::number(job.length, 'g', 4),
                     CliFlag::PreviewDir, m_previewDir.path()};
    if (m_compileCheck->isChecked())
        args << CliFlag::CompileOut;
    if (job.overwrite)
        args << CliFlag::Overwrite;
    args << m_lookupArgs << source << outDir;

    for (const QString &target : job.targets) {
        const int i = tileIndex(target);
        if (i >= 0) {
            m_tiles[i].error.clear();
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
        t.status->setText(tr("Making..."));
        m_statusLabel->setText(tr("Making %1...").arg(evt.value(QStringLiteral("file")).toString()));
    } else if (type == QLatin1String("done")) {
        t.output = evt.value(QStringLiteral("output")).toString();
        t.error.clear();
        t.warnings.clear();
        const QJsonArray ws = evt.value(QStringLiteral("result")).toObject().value(QStringLiteral("warnings")).toArray();
        for (const QJsonValue &w : ws)
            t.warnings << w.toObject().value(QStringLiteral("message")).toString();
        show();
        refreshTileStatus(t);
        ++m_written;
        m_progress->setValue(++m_jobDone);
    } else if (type == QLatin1String("error")) {
        t.error = friendlyError(evt.value(QStringLiteral("message")).toString());
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
