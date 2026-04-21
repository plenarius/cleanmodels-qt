#include "constants.h"
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
            appendDebugHtml("<span>" % QString::fromUtf8(line).toHtmlEscaped() % "</span><br>");
            continue;
        }

        QJsonObject evt = doc.object();
        QString type = evt["type"].toString();
        QString file = evt["file"].toString();

        bool decompiling = m_radioDecompile->isChecked();
        bool compiling = m_radioCompile->isChecked();
        QString actionVerbPast = compiling ? "Compiled" : decompiling ? "Decompiled" : "Cleaned";
        QString actionVerbPresent = compiling ? "Compiling" : decompiling ? "Decompiling" : "Cleaning";

        if (type == "start")
        {
            m_cleanTimer.start();
            m_sCurrentModel = file;
            m_pCleanStatus->setText("Processing " % file);
            m_pStatusProgress->setVisible(true);

            int idx = evt["index"].toInt();
            int total = evt["total"].toInt();
            if (total > 0)
            {
                m_pStatusProgress->setRange(0, total);
                m_pStatusProgress->setValue(idx);
            }

            appendDebugHtml("<p><span style=\"" % QLatin1String(LogColor::Info) % ";\"><b>Processing " % file.toHtmlEscaped() % "</b></span></p><br>");

            int row = findModelRow(file);
            if (row >= 0)
            {
                auto *item = new QTableWidgetItem();
                item->setText(QString::fromUtf8("⏳ ") + actionVerbPresent);
                item->setToolTip(actionVerbPresent);
                m_filesTable->setItem(row, 2, item);
                m_filesTable->scrollToItem(item);
            }
        }
        else if (type == "done")
        {
            m_nMdlsCleaned++;
            m_mdlsCleanedLabel->setText(actionVerbPast % ": " % QString::number(m_nMdlsCleaned));

            int fixes = evt["fixes"].toInt();
            QString elapsedStr = QTime(0, 0).addMSecs(m_cleanTimer.elapsed()).toString("mm:ss.zzz");

            QStringList fixDetails;
            QStringList findings;
            QStringList allTooltipLines;
            QJsonObject result = evt["result"].toObject();
            QJsonArray checks = result["checks"].toArray();
            for (const auto &c : checks)
            {
                QJsonObject check = c.toObject();
                QString msg = check["message"].toString();
                if (check["fixed"].toBool())
                {
                    fixDetails << msg;
                    allTooltipLines << QString::fromUtf8("\xe2\x9c\x93 ") % msg;
                }
                else
                {
                    QString sevLabel;
                    QJsonValue sevVal = check["severity"];
                    if (sevVal.isString()) {
                        QString s = sevVal.toString();
                        sevLabel = (s == "error" || s == "fatal") ? "ERROR" : s == "warning" ? "WARN" : "INFO";
                    } else {
                        int sev = sevVal.toInt();
                        sevLabel = sev >= 2 ? "ERROR" : sev == 1 ? "WARN" : "INFO";
                    }
                    findings << QString("%1: %2").arg(sevLabel, msg);
                    allTooltipLines << QString("%1: %2").arg(sevLabel, msg);
                }
            }
            QJsonArray repairs = result["repairs"].toArray();
            for (const auto &r : repairs)
            {
                fixDetails << r.toString();
                allTooltipLines << QString::fromUtf8("\xe2\x9c\x93 ") % r.toString();
            }

            appendDebugHtml("<p><span style=\"" % QLatin1String(LogColor::Success) % ";\"><b>" % file.toHtmlEscaped() % " " % actionVerbPast.toLower() % " (" % QString::number(fixes) % " fixes)</b></span></p>");

            QString detailHtml;
            if (!fixDetails.isEmpty())
            {
                detailHtml += "<ul style=\"margin:0 0 0 16px; padding:0;\">";
                for (const QString &d : fixDetails)
                    detailHtml += "<li style=\"color:" % QLatin1String(LogColor::FixApplied) % ";\">" % d.toHtmlEscaped() % "</li>";
                detailHtml += "</ul>";
            }
            if (!findings.isEmpty())
            {
                detailHtml += "<ul style=\"margin:0 0 0 16px; padding:0;\">";
                for (const QString &f : findings)
                {
                    QLatin1String color(f.startsWith("ERROR") ? LogColor::SevError : f.startsWith("WARN") ? LogColor::SevWarning : LogColor::SevInfo);
                    detailHtml += "<li style=\"color:" % color % ";\">" % f.toHtmlEscaped() % "</li>";
                }
                detailHtml += "</ul>";
            }
            if (!detailHtml.isEmpty())
                appendDebugHtml(detailHtml % "<br>");
            else
                appendDebugHtml("<br>");

            // Store per-file results for the detail panel
            QStringList fileHtmlLines;
            fileHtmlLines << "<p><b>" % file.toHtmlEscaped() % "</b> — " % actionVerbPast.toLower() % " (" % QString::number(fixes) % " fixes, " % elapsedStr % ")</p>";
            if (!detailHtml.isEmpty())
                fileHtmlLines << detailHtml;
            m_fileResults[file] = fileHtmlLines;

            int selectedRow = m_filesTable->currentRow();
            if (selectedRow >= 0 && selectedRow < m_filesTable->rowCount()
                && m_filesTable->item(selectedRow, 0)->data(Qt::UserRole).toString() == file)
                showFileDetails(file);

            int row = findModelRow(file);
            if (row >= 0)
            {
                auto *statusItem = new QTableWidgetItem();
                statusItem->setText(QString::fromUtf8("✅ ") + actionVerbPast);
                statusItem->setToolTip(actionVerbPast);
                m_filesTable->setItem(row, 2, statusItem);

                QString fixesText = QString::number(fixes);
                if (!findings.isEmpty())
                    fixesText += " (" % QString::number(findings.size()) % " info)";
                auto *fixesItem = new QTableWidgetItem(fixesText);
                fixesItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
                if (!allTooltipLines.isEmpty())
                    fixesItem->setToolTip(allTooltipLines.join("\n"));
                m_filesTable->setItem(row, 3, fixesItem);

                auto *timerItem = new QTableWidgetItem(elapsedStr);
                timerItem->setTextAlignment(Qt::AlignCenter);
                m_filesTable->setItem(row, 4, timerItem);
            }
        }
        else if (type == "error")
        {
            m_nMdlsFailed++;
            m_mdlsFailedLabel->setText("Failed: " % QString::number(m_nMdlsFailed));

            QString msg = evt["message"].toString();
            QString elapsedStr = QTime(0, 0).addMSecs(m_cleanTimer.elapsed()).toString("mm:ss.zzz");

            appendDebugHtml("<p><span style=\"" % QLatin1String(LogColor::Error) % ";\"><b>" % file.toHtmlEscaped() % ": " % msg.toHtmlEscaped() % "</b></span></p><br>");

            QStringList errorHtml;
            errorHtml << "<p style='color:" % QLatin1String(LogColor::SevError) % ";'><b>" % file.toHtmlEscaped() % " — Error</b></p>";
            errorHtml << "<p>" % msg.toHtmlEscaped() % "</p>";
            m_fileResults[file] = errorHtml;

            int selectedRow = m_filesTable->currentRow();
            if (selectedRow >= 0 && selectedRow < m_filesTable->rowCount()
                && m_filesTable->item(selectedRow, 0)->data(Qt::UserRole).toString() == file)
                showFileDetails(file);

            int row = findModelRow(file);
            if (row >= 0)
            {
                auto *item = new QTableWidgetItem();
                item->setText(QString::fromUtf8("❌ Failed"));
                item->setToolTip(msg);
                m_filesTable->setItem(row, 2, item);

                auto *timerItem = new QTableWidgetItem(elapsedStr);
                timerItem->setTextAlignment(Qt::AlignCenter);
                m_filesTable->setItem(row, 4, timerItem);
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

    if (m_radioDecompile->isChecked())
    {
        args << CliCommand::Decompile << CliFlag::JsonLines;
    }
    else if (m_radioCompile->isChecked())
    {
        args << CliCommand::Compile << CliFlag::JsonLines;
    }
    else
    {
        args << CliCommand::Repair << CliFlag::JsonLines;
        // Validation checks
        if (m_checkValidate->isChecked())
            args << CliFlag::Check;

        // Individual fix flags
        if (m_checkStripDegen->isChecked())
            args << CliFlag::StripDegenerate;
        if (m_checkFixAnims->isChecked())
            args << CliFlag::FixAnimations;
        if (m_checkRepairPivots->isChecked())
            args << CliFlag::FixPivots;
        if (m_checkFixTilefade->isChecked())
        {
            args << CliFlag::FixTilefade;
            double h = m_sliceHeightSpin->value() * 0.1;
            if (qAbs(h - 5.0) > 0.01)
                args << CliFlag::TilefadeZ << QString::number(h, 'g', 6);
        }
        if (m_checkTilefadeUndo->isChecked())
            args << CliFlag::TilefadeUndo;
        if (m_checkRebuildAABB->isChecked())
            args << CliFlag::FixAabb;
        if (m_checkReparentChildren->isChecked())
            args << CliFlag::ReparentChildren;
        if (m_checkWrapRoot->isChecked())
            args << CliFlag::WrapRoot;
        if (m_checkSplitMultiEdge->isChecked())
            args << CliFlag::SplitMultiedge;

        // Per-axis scaling
        double sx = m_scaleXSpin->value(), sy = m_scaleYSpin->value(), sz = m_scaleZSpin->value();
        bool allSame = qAbs(sx - sy) < 0.001 && qAbs(sy - sz) < 0.001;
        if (allSame && qAbs(sx - 1.0) > 0.001)
        {
            args << CliFlag::Scale << QString::number(sx, 'g', 6);
        }
        else
        {
            if (qAbs(sx - 1.0) > 0.001)
                args << CliFlag::ScaleX << QString::number(sx, 'g', 6);
            if (qAbs(sy - 1.0) > 0.001)
                args << CliFlag::ScaleY << QString::number(sy, 'g', 6);
            if (qAbs(sz - 1.0) > 0.001)
                args << CliFlag::ScaleZ << QString::number(sz, 'g', 6);
        }

        // Classification override
        QString classVal = m_classificationCombo->currentData().toString();
        if (!classVal.isEmpty())
            args << CliFlag::Classification << classVal;

        // Snap
        QString snapVal = m_snapCombo->currentData().toString();
        if (!snapVal.isEmpty())
            args << CliFlag::Snap << snapVal;

        // TVert snap
        QString tvertVal = m_tvertSnapCombo->currentData().toString();
        if (!tvertVal.isEmpty())
            args << CliFlag::TvertSnap << tvertVal;

        // Render override
        QString renderVal = m_renderCombo->currentData().toString();
        if (!renderVal.isEmpty())
            args << CliFlag::Render << renderVal;

        // Shadow override
        QString shadowVal = m_shadowCombo->currentData().toString();
        if (!shadowVal.isEmpty())
            args << CliFlag::Shadow << shadowVal;

        // Mesh operations
        if (m_forceWhiteCheck->isChecked())
            args << CliFlag::ForceWhite;
        if (m_mergeByBitmapCheck->isChecked())
            args << CliFlag::MergeByBitmap;
        if (m_cullInvisibleCheck->isChecked())
            args << CliFlag::CullInvisible;

        // Placeable transparency
        if (m_placeableTransCheck->isChecked())
        {
            args << CliFlag::PlaceableTrans;
            if (!m_transparencyKeyEdit->text().isEmpty())
                args << CliFlag::TransparencyKey << m_transparencyKeyEdit->text();
        }

        // Pivot sub-options
        if (m_checkRepairPivots->isChecked())
        {
            if (m_pivotAllowSplitCheck->isChecked())
                args << CliFlag::PivotAllowSplit;

            QString belowZ0 = m_pivotBelowZ0Combo->currentData().toString();
            if (m_pivotBelowZ0Combo->currentIndex() > 0)
                args << CliFlag::PivotBelowZ0 << belowZ0;

            QString moveBad = m_pivotMoveBadCombo->currentData().toString();
            if (m_pivotMoveBadCombo->currentIndex() > 0)
                args << CliFlag::PivotMoveBad << moveBad;

            QString smoothing = m_pivotSmoothingCombo->currentData().toString();
            if (m_pivotSmoothingCombo->currentIndex() > 0)
                args << CliFlag::PivotSmoothing << smoothing;

            args << CliFlag::PivotMinFaces << QString::number(m_pivotMinFacesSpin->value());

            QString splitFirst = m_pivotSplitFirstCombo->currentData().toString();
            if (m_pivotSplitFirstCombo->currentIndex() > 0)
                args << CliFlag::PivotSplitFirst << splitFirst;
        }

        // Tile options
        if (m_waterEnableCheck->isChecked())
        {
            args << CliFlag::Water;
            if (!m_waterKeyEdit->text().isEmpty())
                args << CliFlag::WaterKey << m_waterKeyEdit->text();
            QString dynVal = m_dynamicWaterCombo->currentData().toString();
            args << CliFlag::DynamicWater << dynVal;
            if (dynVal == "wavy")
                args << CliFlag::WaveHeight << QString::number(m_waveHeightSpin->value(), 'g', 4);
            QString rotW = m_rotateWaterCombo->currentData().toString();
            if (!rotW.isEmpty())
                args << CliFlag::RotateWater << rotW;
            QString retileW = m_retileWaterCombo->currentData().toString();
            if (!retileW.isEmpty())
                args << CliFlag::RetileWater << retileW;
        }

        QString foliageVal = m_foliageCombo->currentData().toString();
        if (!foliageVal.isEmpty())
        {
            args << CliFlag::Foliage << foliageVal;
            if (foliageVal != "ignore" && !m_foliageKeyEdit->text().isEmpty())
                args << CliFlag::FoliageKey << m_foliageKeyEdit->text();
        }

        if (m_animateSplotchesCheck->isChecked())
        {
            args << CliFlag::Splotch << "animate";
            if (!m_splotchKeyEdit->text().isEmpty())
                args << CliFlag::SplotchKey << m_splotchKeyEdit->text();
        }

        QString rotGround = m_rotateGroundCombo->currentData().toString();
        if (!rotGround.isEmpty())
        {
            args << CliFlag::RotateGround << rotGround;
            if (!m_groundKeyEdit->text().isEmpty())
                args << CliFlag::GroundKey << m_groundKeyEdit->text();
        }

        QString chamferVal = m_chamferCombo->currentData().toString();
        if (!chamferVal.isEmpty())
            args << CliFlag::Chamfer << chamferVal;

        QString retileG = m_retileGroundCombo->currentData().toString();
        if (!retileG.isEmpty())
            args << CliFlag::RetileGround << retileG;

        QString raiseLowerVal = m_raiseLowerCombo->currentData().toString();
        if (!raiseLowerVal.isEmpty())
        {
            args << CliFlag::RaiseLower << raiseLowerVal;
            args << CliFlag::RaiseAmount << QString::number(m_raiseAmountSpin->value(), 'g', 4);
        }

        if (m_remapWokMatCheck->isChecked())
            args << CliFlag::RemapWalkmesh << QString("%1:%2").arg(m_wokMatFromSpin->value()).arg(m_wokMatToSpin->value());
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
        m_debugTextBrowser->append("Aborted");
        auto *twiCleanAborted = new QTableWidgetItem();
        twiCleanAborted->setText("Aborted");
        twiCleanAborted->setIcon(m_iconAbortButton);
        int row = findModelRow(m_sCurrentModel);
        if (row >= 0)
            m_filesTable->setItem(row, 2, twiCleanAborted);
        return;
    }

    disconnect(m_pCleanProcess, &QProcess::readyReadStandardOutput,
               this, &MainWindow::onCaptureCleanModelsOutput);
    connect(m_pCleanProcess, &QProcess::readyReadStandardOutput,
            this, &MainWindow::onCaptureCleanModelsOutput);

    m_stdoutBuffer.clear();
    m_debugTextBrowser->clear();
    m_debugTextBrowser->insertHtml("Running cleanmodels<br>");
    m_fileResults.clear();
    m_batchSummary.clear();
    m_detailPanel->clear();

    if (!m_rawLogVisible)
        toggleRawLog();

    QStringList args = buildCliArgs();

    appendDebugHtml("<span style=\"" % QLatin1String(LogColor::Command) % ";\">$ " % m_sBinaryPath.toHtmlEscaped() % " " % args.join(" ").toHtmlEscaped() % "</span><br>");

    m_pCleanProcess->setWorkingDirectory(QDir::currentPath());
    m_pCleanProcess->start(m_sBinaryPath, args, QIODevice::ReadOnly);
    m_cleanButton->setDisabled(true);

    if (m_pCleanProcess->waitForStarted())
    {
        m_nMdlsCleaned = 0;
        m_nMdlsFailed = 0;
        m_mdlsCleanedLabel->setText("Cleaned: 0");
        m_mdlsFailedLabel->setText("Failed: 0");
        m_bCleanRunning = true;
        m_cleanButton->setDisabled(false);
        m_cleanButton->setText("Abort");
        m_cleanButton->setIcon(m_iconAbortButton);
        m_cleanButton->setStyleSheet("QPushButton { color: #e74c3c; font-weight: bold; }");
        m_pStatusProgress->setRange(0, 0);
        m_pStatusProgress->setVisible(true);
    }
    else
    {
        QString errorMsg = "<p><span style=\"" % QLatin1String(LogColor::Error) % ";\">Failed to run cleanmodels! Does the " %
            m_sBinaryName % " executable exist in the working directory or your PATH?</span></p><br>";
        appendDebugHtml(errorMsg);
        m_detailPanel->setHtml("<p style='color:" + QLatin1String(LogColor::SevError) + ";'><b>Failed to start cleanmodels.</b><br>"
                               "Ensure the <code>" + m_sBinaryName.toHtmlEscaped() + "</code> executable is in your PATH or alongside this application.</p>");
        m_cleanButton->setDisabled(false);
    }
}

void MainWindow::onCleanFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    onCaptureCleanModelsOutput();

    QString stderrOutput = QString::fromUtf8(m_pCleanProcess->readAllStandardError()).trimmed();
    if (!stderrOutput.isEmpty())
        m_debugTextBrowser->append(stderrOutput);

    m_bCleanRunning = false;

    updateModeUI();
    m_cleanButton->setStyleSheet("");
    m_pCleanStatus->setText("Idle");
    m_pStatusProgress->setVisible(false);

    if (exitStatus == QProcess::CrashExit) {
        QString msg = "<p style='color:" + QLatin1String(LogColor::SevError) + ";'><b>cleanmodels crashed.</b></p>";
        if (!stderrOutput.isEmpty())
            msg += "<pre style='color:" + QLatin1String(LogColor::SevError) + ";'>" + stderrOutput.toHtmlEscaped() + "</pre>";
        m_detailPanel->setHtml(msg);
        appendDebugHtml(msg);
    } else if (exitCode != 0 && m_nMdlsCleaned == 0 && m_nMdlsFailed == 0) {
        QString msg = "<p style='color:" + QLatin1String(LogColor::SevError) + ";'><b>cleanmodels exited with error code " +
                      QString::number(exitCode) + "</b></p>";
        if (!stderrOutput.isEmpty())
            msg += "<pre style='color:" + QLatin1String(LogColor::SevError) + ";'>" + stderrOutput.toHtmlEscaped() + "</pre>";
        else
            msg += "<p>No output was produced. Check the CLI flags and try running from a terminal.</p>";
        m_detailPanel->setHtml(msg);
        appendDebugHtml(msg);
    }

    m_batchSummary = QString("%1 files cleaned, %2 failed")
        .arg(m_nMdlsCleaned)
        .arg(m_nMdlsFailed);

    if (m_filesTable->currentRow() < 0)
        showBatchSummary();

    if (m_bUpdateFilesAfterClean)
    {
        updateFileListing();
        m_bUpdateFilesAfterClean = false;
    }
}

int MainWindow::findModelRow(const QString &mdlFile)
{
    int rows = m_filesTable->rowCount();
    for (int i = 0; i < rows; ++i)
    {
        if (m_filesTable->item(i, 0)->data(Qt::UserRole).toString() == mdlFile)
            return i;
    }
    return -1;
}

void MainWindow::appendDebugHtml(const QString &html)
{
    m_debugTextBrowser->insertHtml(html);
    auto sb = m_debugTextBrowser->verticalScrollBar();
    sb->setValue(sb->maximum());
}
