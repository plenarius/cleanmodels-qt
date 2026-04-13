#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringBuilder>
#include <QScrollBar>
#include <QTime>

void MainWindow::onCaptureCleanModelsOutput()
{
    if (!m_pCleanProcess)
        return;

    m_stdoutBuffer.append(m_pCleanProcess->readAllStandardOutput());

    while (true)
    {
        int nlPos = m_stdoutBuffer.indexOf('\n');
        if (nlPos < 0)
            break;

        QByteArray line = m_stdoutBuffer.left(nlPos).trimmed();
        m_stdoutBuffer.remove(0, nlPos + 1);

        if (line.isEmpty())
            continue;

        QJsonParseError jsonErr;
        QJsonDocument doc = QJsonDocument::fromJson(line, &jsonErr);
        if (jsonErr.error != QJsonParseError::NoError || !doc.isObject())
        {
            appendDebugHtml("<span>" % QString::fromUtf8(line) % "</span><br>");
            continue;
        }

        QJsonObject evt = doc.object();
        QString type = evt["type"].toString();
        QString file = evt["file"].toString();

        QString actionVerbPast = ui->decompileCheck->isChecked() ? "Decompiled" : "Cleaned";
        QString actionVerbPresent = ui->decompileCheck->isChecked() ? "Decompiling" : "Cleaning";
        QIcon actionIcon = ui->decompileCheck->isChecked() ? m_iconDecompilingMDL : m_iconCleaningMDL;

        if (type == "start")
        {
            m_cleanTimer.start();
            m_sCurrentModel = file;
            m_pCleanStatus->setText(tr("Processing ") % file);
            m_pStatusProgress->setVisible(true);

            int idx = evt["index"].toInt();
            int total = evt["total"].toInt();
            if (total > 0)
            {
                m_pStatusProgress->setRange(0, total);
                m_pStatusProgress->setValue(idx);
            }

            appendDebugHtml("<p><span style=\"color:blue;\"><b>Processing " % file % "</b></span></p><br>");

            int row = findModelRow(file);
            if (row >= 0)
            {
                auto *item = new QTableWidgetItem();
                item->setIcon(m_iconReadingMDL);
                item->setToolTip(actionVerbPresent);
                item->setText(actionVerbPresent);
                ui->filesTable->setItem(row, 2, item);
                ui->filesTable->scrollToItem(item);
            }
        }
        else if (type == "done")
        {
            m_nMdlsCleaned++;
            ui->mdlsCleanedLabel->setText("Files " % actionVerbPast % ": " % QString::number(m_nMdlsCleaned));

            int fixes = evt["fixes"].toInt();
            QString elapsedStr = QTime(0,0).addMSecs(m_cleanTimer.elapsed()).toString("mm:ss.zzz");

            appendDebugHtml("<p><span style=\"color:green;\"><b>" % file % " " % actionVerbPast.toLower() % " (" % QString::number(fixes) % " fixes)</b></span></p><br>");

            int row = findModelRow(file);
            if (row >= 0)
            {
                auto *statusItem = new QTableWidgetItem();
                statusItem->setText(actionVerbPast);
                statusItem->setIcon(m_iconCleanSuccess);
                statusItem->setToolTip(actionVerbPast);
                ui->filesTable->setItem(row, 2, statusItem);

                auto *fixesItem = new QTableWidgetItem(QString::number(fixes));
                fixesItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
                ui->filesTable->setItem(row, 3, fixesItem);

                auto *timerItem = new QTableWidgetItem(elapsedStr);
                timerItem->setTextAlignment(Qt::AlignCenter);
                ui->filesTable->setItem(row, 4, timerItem);
            }
        }
        else if (type == "error")
        {
            m_nMdlsFailed++;
            ui->mdlsFailedLabel->setText("Failures: " % QString::number(m_nMdlsFailed));

            QString msg = evt["message"].toString();
            QString elapsedStr = QTime(0,0).addMSecs(m_cleanTimer.elapsed()).toString("mm:ss.zzz");

            appendDebugHtml("<p><span style=\"color:red;\"><b>" % file % ": " % msg.toHtmlEscaped() % "</b></span></p><br>");

            int row = findModelRow(file);
            if (row >= 0)
            {
                auto *item = new QTableWidgetItem();
                item->setText(tr("Failed"));
                item->setIcon(m_iconCleanError);
                item->setToolTip(msg);
                ui->filesTable->setItem(row, 2, item);

                auto *timerItem = new QTableWidgetItem(elapsedStr);
                timerItem->setTextAlignment(Qt::AlignCenter);
                ui->filesTable->setItem(row, 4, timerItem);
            }
        }
        else if (type == "summary")
        {
            QString msg = evt["message"].toString();
            appendDebugHtml("<p><b>" % msg.toHtmlEscaped() % "</b></p><br>");
        }
    }
}

QStringList MainWindow::buildCliArgs()
{
    QStringList args;
    args << "--json-lines";

    if (ui->decompileCheck->isChecked())
    {
        args << "--decompile-only";
    }
    else
    {
        args << "--check";

        if (ui->rescaleXSpin->value() != 1 || ui->rescaleYSpin->value() != 1 || ui->rescaleZSpin->value() != 1)
        {
            double avg = (ui->rescaleXSpin->value() + ui->rescaleYSpin->value() + ui->rescaleZSpin->value()) / 3.0;
            args << "--scale" << QString::number(avg, 'g', 6);
        }

        if (ui->repairAABBCombo->currentIndex() == 1 || ui->repairAABBCombo->currentIndex() == 2)
            args << "--fix-aabb";

        if (ui->repivotCombo->currentIndex() == 1)
            args << "--fix-pivots";

        if (ui->sliceForTileFadeCombo->currentIndex() == 0)
        {
            args << "--fix-tilefade";
        }

        args << "--strip-degenerate";
        args << "--fix-animations";
        args << "--reparent-children";
        args << "--wrap-root";
        args << "--split-multiedge";
    }

    args << m_sInDir;

    if (!m_sOutDir.isEmpty() && m_sOutDir != m_sInDir)
        args << m_sOutDir;

    return args;
}

void MainWindow::doClean()
{
    if (m_bCleanRunning)
    {
        m_pCleanProcess->kill();
        ui->debugTextBrowser->append(tr("Aborted"));
        ui->decompileCheck->setEnabled(true);
        auto *twiCleanAborted = new QTableWidgetItem();
        twiCleanAborted->setText(tr("Aborted"));
        twiCleanAborted->setIcon(m_iconAbortButton);
        twiCleanAborted->setToolTip(tr("Aborted"));
        int row = findModelRow(m_sCurrentModel);
        if (row >= 0)
            ui->filesTable->setItem(row, 2, twiCleanAborted);
        return;
    }

    connect(m_pCleanProcess, &QProcess::readyReadStandardOutput,
            this, &MainWindow::onCaptureCleanModelsOutput);

    m_stdoutBuffer.clear();
    ui->debugTextBrowser->clear();
    ui->debugTextBrowser->insertHtml(tr("Running cleanmodels<br>"));

    QStringList args = buildCliArgs();

    appendDebugHtml("<span style=\"color:gray;\">$ " % m_sBinaryPath % " " % args.join(" ") % "</span><br>");

    m_pCleanProcess->setWorkingDirectory(QDir::currentPath());
    m_pCleanProcess->start(m_sBinaryPath, args, QIODevice::ReadOnly);
    ui->cleanButton->setDisabled(true);

    if (m_pCleanProcess->waitForStarted())
    {
        ui->decompileCheck->setEnabled(false);
        m_nMdlsCleaned = 0;
        m_nMdlsFailed = 0;
        ui->mdlsCleanedLabel->setText("Files Cleaned: 0");
        ui->mdlsFailedLabel->setText("Failures: 0");
        m_bCleanRunning = true;
        ui->cleanButton->setDisabled(false);
        ui->cleanButton->setText(tr("Abort"));
        ui->cleanButton->setIcon(m_iconAbortButton);
        m_pStatusProgress->setRange(0, 0);
        m_pStatusProgress->setVisible(true);
    }
    else
    {
        QString errorMsg = "<p><span style=\"color:red;\">Failed to run cleanmodels! Does the " %
            m_sBinaryName % " executable exist in the working directory or your PATH?</span></p><br>" % m_sBinaryPath;
        ui->debugTextBrowser->insertHtml(tr(errorMsg.toStdString().c_str()));
        auto sb = ui->debugTextBrowser->verticalScrollBar();
        sb->setValue(sb->maximum());
        ui->cleanButton->setDisabled(false);
    }
}

void MainWindow::onCleanFinished(int, QProcess::ExitStatus)
{
    onCaptureCleanModelsOutput();

    ui->debugTextBrowser->append(m_pCleanProcess->readAllStandardError());
    m_bCleanRunning = false;

    if (!ui->decompileCheck->isChecked())
        ui->cleanButton->setText(tr("Clean"));
    else
        ui->cleanButton->setText(tr("Decompile"));
    ui->decompileCheck->setEnabled(true);
    ui->cleanButton->setIcon(m_iconCleanButton);
    m_pCleanStatus->setText(tr("Idle"));
    m_pStatusProgress->setVisible(false);

    if (m_bUpdateFilesAfterClean)
    {
        MainWindow::updateFileListing();
        m_bUpdateFilesAfterClean = false;
    }
}

int MainWindow::findModelRow(const QString& mdlFile)
{
    int rows = ui->filesTable->rowCount();
    for (int i = 0; i < rows; ++i)
    {
        if (ui->filesTable->item(i, 0)->text() == mdlFile)
            return i;
    }
    return -1;
}

void MainWindow::appendDebugHtml(const QString& html)
{
    ui->debugTextBrowser->insertHtml(html);
    auto sb = ui->debugTextBrowser->verticalScrollBar();
    sb->setValue(sb->maximum());
}
