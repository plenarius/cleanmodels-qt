#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDirIterator>
#include <QStringBuilder>
#include <QScrollBar>
#include <QTime>

using namespace std;


void MainWindow::onCaptureCleanModelsOutput()
{
    if (m_pCleanProcess)
    {
        auto outPut = QString::fromStdString(m_pCleanProcess->readAllStandardOutput().toStdString());
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 2))
        QStringList lines = outPut.split( "\n", Qt::SkipEmptyParts );
#else
        QStringList lines = outPut.split( "\n", QString::SkipEmptyParts );
#endif
        foreach( QString line, lines )
        {
            if (line == ".")
                continue;
            QRegExp rx_dot(R"(^((.)\2+)+$)");
            auto pos = rx_dot.indexIn(line);
            if (pos > -1)
                continue;
            QString sStatus;
            QString outputHtml;
            QRegExp rx_reading("Attempting to read (.*)");
            pos = rx_reading.indexIn(line);
            if (pos > -1)
            {
                m_cleanTimer.start();
                m_sCurrentModel = rx_reading.cap(1).trimmed();
                sStatus = tr("Reading ") % m_sCurrentModel;
                m_pCleanStatus->setText(sStatus);
                m_pStatusProgress->setVisible(true);
                outputHtml = "<p><span style=\"color:blue;\"><b>" % line % "</b></span></p><br>";
                ui->debugTextBrowser->insertHtml(outputHtml);
                auto sb = ui->debugTextBrowser->verticalScrollBar();
                sb->setValue(sb->maximum());
                auto *twiReadingMDL = new QTableWidgetItem();
                twiReadingMDL->setIcon(m_iconReadingMDL);
                twiReadingMDL->setToolTip("Reading");
                twiReadingMDL->setText(tr("Reading"));
                ui->filesTable->setItem(findModelRow(m_sCurrentModel), 2, twiReadingMDL);
                ui->filesTable->scrollToItem(twiReadingMDL);
                continue;
            }
            QRegExp rx_mdl("MDL\\s(.*)\\sloaded.");
            pos = rx_mdl.indexIn(line);
            if (pos > -1)
            {
                sStatus = tr("Cleaning ") % m_sCurrentModel;
                m_pCleanStatus->setText(sStatus);
                m_pStatusProgress->setVisible(true);
                outputHtml = "<p><span style=\"color:blue;\"><b>" % line % "</b></span></p><br>";
                ui->debugTextBrowser->insertHtml(outputHtml);
                auto sb = ui->debugTextBrowser->verticalScrollBar();
                sb->setValue(sb->maximum());
                auto *twiCleaningMDL = new QTableWidgetItem();
                twiCleaningMDL->setText(tr("Cleaning"));
                twiCleaningMDL->setIcon(m_iconCleaningMDL);
                twiCleaningMDL->setToolTip(tr("Cleaning"));
                ui->filesTable->setItem(findModelRow(m_sCurrentModel), 2, twiCleaningMDL);
                continue;
            }
            QRegExp rx_bin("Binary file (.*) detected, attempting import.");
            pos = rx_bin.indexIn(line);
            if (pos > -1)
            {
                sStatus = tr("Decompiling ") % rx_bin.cap(1);
                m_pStatusProgress->setVisible(true);
                m_pCleanStatus->setText(sStatus);
            }
            QRegExp rx_fixes(R"(Fixes made = (\d+))");
            pos = rx_fixes.indexIn(line);
            if (pos > -1)
            {
                auto *fixesItem = new QTableWidgetItem(rx_fixes.cap(1));
                fixesItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
                ui->filesTable->setItem(findModelRow(m_sCurrentModel), 3, fixesItem);
            }
            QRegExp rx_written(R"((.*) written.)");
            pos = rx_written.indexIn(line);
            if (pos > -1)
            {
                m_nMdlsCleaned++;
                ui->mdlsCleanedLabel->setText("Files Cleaned: " % QString::number(m_nMdlsCleaned));
                outputHtml = "<p><span style=\"color:green;\"><b>" % line % "</b></span></p><br>";
                ui->debugTextBrowser->insertHtml(outputHtml);
                auto sb = ui->debugTextBrowser->verticalScrollBar();
                sb->setValue(sb->maximum());
                auto *twiCleanSuccess = new QTableWidgetItem();
                twiCleanSuccess->setText(tr("Cleaned"));
                twiCleanSuccess->setIcon(m_iconCleanSuccess);
                twiCleanSuccess->setToolTip(tr("Cleaned"));
                auto *twiCleanTimer = new QTableWidgetItem();
                twiCleanTimer->setText(QTime(0,0).addMSecs(m_cleanTimer.elapsed()).toString("mm:ss.zzz"));
                twiCleanTimer->setTextAlignment(Qt::AlignCenter);
                ui->filesTable->setItem(findModelRow(m_sCurrentModel), 2, twiCleanSuccess);
                ui->filesTable->setItem(findModelRow(m_sCurrentModel), 4, twiCleanTimer);
                continue;
            }
            QRegExp rx_error(R"(\*\*\* Cannot(.*)|\*\* Load failed(.*))");
            pos = rx_error.indexIn(line);
            if (pos > -1)
            {
                m_nMdlsFailed++;
                ui->mdlsFailedLabel->setText("Failures: " % QString::number(m_nMdlsFailed));
                outputHtml = "<p><span style=\"color:red;\"><b>" % line % "</b></span></p><br>";
                auto *twiCleanError = new QTableWidgetItem();
                twiCleanError->setText(tr("Failed"));
                twiCleanError->setIcon(m_iconCleanError);
                twiCleanError->setToolTip(tr("Failed"));
                auto *twiCleanTimer = new QTableWidgetItem();
                twiCleanTimer->setText(QTime(0,0).addMSecs(m_cleanTimer.elapsed()).toString("mm:ss.zzz"));
                twiCleanTimer->setTextAlignment(Qt::AlignCenter);
                ui->filesTable->setItem(findModelRow(m_sCurrentModel), 2, twiCleanError);
                ui->filesTable->setItem(findModelRow(m_sCurrentModel), 4, twiCleanTimer);
            }
            else
            {
                outputHtml = "<span>" % line % "</span><br>";
            }
            ui->debugTextBrowser->insertHtml(outputHtml);
            auto sb = ui->debugTextBrowser->verticalScrollBar();
            sb->setValue(sb->maximum());
        }
    }
}

void MainWindow::doClean()
{
    if (m_bCleanRunning)
    {
        m_pCleanProcess->kill();
        ui->debugTextBrowser->append(tr("Aborted"));
        return;
    }
    connect(m_pCleanProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(onCaptureCleanModelsOutput()));
    ui->debugTextBrowser->clear();
    ui->debugTextBrowser->insertHtml(tr("Running cleanmodels<br>"));
    QString command;
    QStringList args;
    m_pCleanProcess->setCurrentWriteChannel(QProcess::StandardOutput);
    m_pCleanProcess->setWorkingDirectory(QDir::currentPath());
    QString filePattern = ui->filePattern->text();
    if (ui->decompileCheck->isChecked())
        args<<"-d";
    else
        args<<"last_dirs.pl";
    m_pCleanProcess->start(m_sBinaryPath,args,QIODevice::ReadWrite);
    ui->cleanButton->setDisabled(true);
    if (m_pCleanProcess->waitForStarted())
    {
        m_nMdlsCleaned = 0;
        m_nMdlsFailed = 0;
        ui->mdlsCleanedLabel->setText("Files Cleaned: 0");
        ui->mdlsFailedLabel->setText("Failures: 0");
        updateFileListing();
        m_bCleanRunning = true;
        ui->cleanButton->setDisabled(false);
        ui->cleanButton->setText(tr("Abort"));
        ui->cleanButton->setIcon(m_iconAbortButton);
    }
    else
    {
        QString errorMsg = "<p><span style=\"color:red;\">Failed to run clean! Does the " % m_sBinaryName % " executable exist in the working directory or your PATH?</span></p><br>" % m_sBinaryPath;
        ui->debugTextBrowser->insertHtml(tr(errorMsg.toStdString().c_str()));
        auto sb = ui->debugTextBrowser->verticalScrollBar();
        sb->setValue(sb->maximum());
        ui->cleanButton->setDisabled(false);
    }
}

void MainWindow::onCleanFinished(int, QProcess::ExitStatus)
{
    ui->debugTextBrowser->append(m_pCleanProcess->readAllStandardError());
    m_bCleanRunning = false;
    ui->cleanButton->setText(tr("Clean"));
    ui->cleanButton->setIcon(m_iconCleanButton);
    m_pCleanStatus->setText(tr("Idle"));
    m_pStatusProgress->setVisible(false);
}

int MainWindow::findModelRow(const QString& mdlFile)
{
    int rows = ui->filesTable->rowCount();
    int foundRow = 0; // fall back to first row if we can't find it for some reason
    for (int i = 0; i < rows; ++i)
    {
        if (ui->filesTable->item(i, 0)->text() == mdlFile)
        {
            foundRow = i;
            break;
        }
    }
    return foundRow;
}