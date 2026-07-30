/******************************************************************************
 * Module      : MainWindow
 * Description : ETBU Operator Interface.
 *               Handles call queue management, call answering,
 *               call termination, log viewing and audio playback.
 * Implemented By: Priyanka
 ******************************************************************************/
#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QDateTime>
#include <QTableWidgetItem>
#include <QMessageBox>
#include "databasemanager.h"
#include <QSqlQuery>
#include <QTableWidgetItem>
#include <QSqlError>
#include <QDebug>
#include <QHeaderView>
#include "gpsmanager.h"
#include <QApplication>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setupLogTable();

    setIdleState();
    // Initialize audio recording module
    m_audioRecorder = new AudioRecorder();
    // Initialize communication object
    comm = new Master_communication(this);
    m_gps = new GpsManager(this);
    m_gps->start("/dev/ttySTM2", 9600);   // change port name to match board

    connect(m_gps, &GpsManager::fixAcquired, this, [this]() {
        qDebug() << "GPS ready";
    });
    connect(m_gps, &GpsManager::fixLost, this, [this]() {
        qDebug() << "GPS lost signal";
    });
    connect(m_gps,
            &GpsManager::locationChanged,
            this,
            [this](const QString &location)
            {
                qDebug() << "GPS Location:" << location;

                m_callStartLocation = location;

                // Replace lblGps with your actual QLabel name
                // ui->lblGps->setText(location);
            });

    if (!m_database.initialize())
    {
        qDebug() << "Database initialization failed";
    }
    //autodelete 30 days old data
    m_database.deleteOldLogs();

    ui->stackedWidget->setCurrentIndex(0); // show dashboard page initially

    clockTimer = new QTimer(this);
    m_playProcess = new QProcess(this);

    connect(m_playProcess, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this]()
            {
                if (m_currentPlayButton)
                {
                    m_currentPlayButton->setText("Play");
                    m_currentPlayButton = nullptr;
                }
            });

    connect(clockTimer,&QTimer::timeout,this,&MainWindow::updateDateTime);
    clockTimer->start(1000);
    updateDateTime();

    connect(ui->btnSettings,&QPushButton::clicked,this,&MainWindow::showSettingsPage);
    connect(ui->btndashboard,&QPushButton::clicked,this,&MainWindow::ShowDashboardpage);
    connect(ui->btnHealth,&QPushButton::clicked,this,&MainWindow::showHealthPage);
    connect(ui->btndashboard1,&QPushButton::clicked,this,&MainWindow::ShowDashboardpage);
    connect(ui->btnlog,&QPushButton::clicked,this,&MainWindow::showLogPage);
    connect(ui->btndashboard2,&QPushButton::clicked,this,&MainWindow::ShowDashboardpage);
    connect(comm,&Master_communication::callQueueUpdated,this,&MainWindow::updateQueueList);
    connect(ui->tableWidget,&QTableWidget::cellClicked,this,&MainWindow::onQueueItemSelected);
    connect(comm,&Master_communication::activeCallChanged,this,&MainWindow::updateActiveCall);

    statusTimer = new QTimer(this);
    connect(statusTimer,&QTimer::timeout,this,&MainWindow::updateEtbuCounts);
    connect(statusTimer,&QTimer::timeout,this,&MainWindow::updateEtbuStatusTable);
    connect(statusTimer,&QTimer::timeout,this,&MainWindow::updateHealthPage);
    statusTimer->start(500);

    // Health page wiring (clause 6.7 / 6.20 / 8.2)
    setupHealthTable();
    connect(comm, &Master_communication::linkFault, this, &MainWindow::onLinkFault);
    connect(comm, &Master_communication::etbuOffline, this, &MainWindow::onEtbuOffline);
    connect(ui->btnClearFaultLog, &QPushButton::clicked, this, &MainWindow::on_btnClearFaultLog_clicked);
    connect(ui->btnExportDiagnostics, &QPushButton::clicked, this, &MainWindow::on_btnExportDiagnostics_clicked);

    // Settings page wiring (clause 6.2 / 8.3 / 6.3 / 6.18)
    connect(ui->btnLogin, &QPushButton::clicked, this, &MainWindow::on_btnLogin_clicked);
    connect(ui->btnLogout, &QPushButton::clicked, this, &MainWindow::on_btnLogout_clicked);
    connect(ui->btnExportLogs, &QPushButton::clicked, this, &MainWindow::on_btnExportLogs_clicked);
    ui->cmbPlaybackDevice->addItem("plughw:0,0");
    populateSettingsPage();

    // Master<->Slave Response Unit (clause 5)
    connect(ui->btnSlaveUnit, &QPushButton::clicked, this, &MainWindow::on_btnSlaveUnit_clicked);
    connect(comm, &Master_communication::roleChanged, this, &MainWindow::onRuRoleChanged);
    connect(comm, &Master_communication::peerStatusChanged, this, &MainWindow::onPeerStatusChanged);
    onRuRoleChanged(comm->isMasterRole());   // set the initial label state

    // Clause 5.2/7.1 - call alarm + 30s (programmable) escalation
    connect(comm, &Master_communication::callAlarmRaised, this, &MainWindow::onCallAlarmRaised);
    connect(comm, &Master_communication::callEscalated, this, &MainWindow::onCallEscalated);
    connect(comm, &Master_communication::callAlarmCleared, this, &MainWindow::onCallAlarmCleared);
    connect(ui->spinAlarmTimeout, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::on_spinAlarmTimeout_valueChanged);
    ui->spinAlarmTimeout->setValue(comm->getEscalationThresholdSecs());

    alarmBlinkTimer = new QTimer(this);
    connect(alarmBlinkTimer, &QTimer::timeout, this, &MainWindow::blinkAlarmBanner);
    alarmBlinkTimer->start(500);   // blink rate for the visual location indication
}


MainWindow::~MainWindow()
{
    if (m_playProcess && m_playProcess->state() != QProcess::NotRunning)
    {
        m_playProcess->kill();
        m_playProcess->waitForFinished();
    }

    m_audioRecorder->stopRecording();

    if (m_recordingActive)
    {
        m_audioRecorder->stopRecording();

        QString endTime =
            QDateTime::currentDateTime()
                .toString(Qt::ISODate);

        m_database.insertLog(
            m_currentEtbuAddress,
            m_callStartTime,
            m_callEndTime,
            "G001",
            //"--",
            m_callStartLocation,
            m_currentAudioFile);
    }

    delete m_audioRecorder;
    delete ui;
}

void MainWindow::showSettingsPage()
{
    populateSettingsPage();
    ui->stackedWidget->setCurrentWidget(ui->pageSettings);
}

void MainWindow::ShowDashboardpage()
{
    ui->stackedWidget->setCurrentWidget(ui->pageDashboard);
}

void MainWindow::showHealthPage()
{
    updateHealthPage();
    ui->stackedWidget->setCurrentWidget(ui->pagehealth);
}
void MainWindow::showLogPage()
{
    loadLogs();
    ui->stackedWidget->setCurrentWidget(ui->pagelog);
}

void MainWindow::updateDateTime()
{
    QDateTime current =QDateTime::currentDateTime();

    ui->lblDate->setText(current.date().toString("dd-MM-yyyy"));

    ui->lblTime->setText(current.time().toString("hh:mm:ss"));
}

void MainWindow::on_btnComm_clicked()
{
    if(comm->isRunning())
    {
        comm->stopCommunication();

        ui->btnComm->setText("START COMM");
        qDebug() << "port closed";
    }
    else
    {
        comm->startCommunication();

        ui->btnComm->setText("STOP COMM");

    }
}

void MainWindow::updateQueueList(const QList<quint8> &queue)
{
    ui->tableWidget->setRowCount(queue.size());

    for(int row = 0; row < queue.size(); row++)
    {
        quint8 address = queue[row];

        ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));

        ui->tableWidget->setItem(row, 1, new QTableWidgetItem("--"));

        ui->tableWidget->setItem(row, 2, new QTableWidgetItem(QString::number(address)));

        ui->tableWidget->setItem(row, 3, new QTableWidgetItem("QUEUED"));

        ui->tableWidget->setItem(row, 4, new QTableWidgetItem("--"));
    }
}

void MainWindow::onQueueItemSelected(int row,int column)
{
    Q_UNUSED(column);

    QTableWidgetItem *item = ui->tableWidget->item(row, 2);

    if(item)
    {
        selectedEtbu = item->text().toUInt();

        qDebug() << "Selected ETBU" << selectedEtbu;
        ui->lblselAnsetbu->setText("SELECTED ETBU : " + item->text());
    }
}

void MainWindow::on_btnanswer_clicked()
{
    if(selectedEtbu == 0)
    {
        QMessageBox::warning(this,"No ETBU Selected","Please select an ETBU from the call queue.");
        qDebug() << "No ETBU Selected";
        return;
    }

    comm->answerCall(selectedEtbu);
    selectedEtbu = 0;
    ui->tableWidget->clearSelection();
    ui->lblselAnsetbu->setText("SELECTED ETBU : None ");
}

void MainWindow::updateActiveCall(quint8 address,const QString &state)
{

    ui->lblActiveETBU->setText(QString("ETBU :-- %1").arg(address));

    ui->lblCallStatus->setText(state);

    if(address < 255)
        setTalkState();

    else
    {
       ui->lblActiveETBU->setText("ETBU :-- None");
        setIdleState();
    }

    if(state == "HOLD")
    {
        ui->btnHold->setText("RESUME");
    }

    if(state == "CALL ACTIVE")
    {
        if (!m_recordingActive)
        {
            m_currentEtbuAddress = address;

            m_callStartTime = QDateTime::currentDateTime()
                                 .toString(Qt::ISODate);
            qDebug() << "GPS Fix =" << m_gps->hasFix(); // added by pooja on 21 july 2026
            qDebug() << "Current GPS =" << m_gps->currentLocationString();// added by pooja on 21 july 2026
            m_callStartLocation = m_gps->currentLocationString();   // <-- GPS
            m_currentAudioFile = m_audioRecorder->generateFilePath(address);

            qDebug() << "[MAIN] Generated file path:" << m_currentAudioFile;

            m_audioRecorder->startRecording(m_currentAudioFile);

            m_recordingActive = true;
        }
    }

    else if(state == "TALK")
    {
        ui->btnHold->setText("HOLD");
    }

}

void MainWindow::on_btnEnd_clicked()
{
    if (m_recordingActive)
    {
        m_audioRecorder->stopRecording();

        m_callEndTime =
            QDateTime::currentDateTime()
                .toString(Qt::ISODate);

        if (m_currentAudioFile.isEmpty())
        {
            qDebug() << "Audio file path is empty";
        }
        else
        {
            bool ok = m_database.insertLog(
                m_currentEtbuAddress,
                m_callStartTime,
                m_callEndTime,
                "G001",
                m_callStartLocation,     //Added by pooja on 20 july 2026 to insert gps log in database
                m_currentAudioFile);

            qDebug() << "Database insert:" << ok;
        }
    }
    comm->endCall();

    m_recordingActive = false;
}

void MainWindow::setIdleState()
{
    ui->btnanswer->setEnabled(true);
    ui->btnEnd->setEnabled(false);
    ui->btnHold->setEnabled(false);
}

void MainWindow::setTalkState()
{
    ui->btnanswer->setEnabled(false);

    ui->btnEnd->setEnabled(true);
    ui->btnHold->setEnabled(true);

}

void MainWindow::on_btnHold_clicked()
{
    if(comm->isCallOnHold())
    {
        comm->resumeCall();
    }
    else
    {
        comm->holdCall();
    }
}

void MainWindow::updateEtbuCounts()
{
    int onlineCount = 0;
    int offlineCount = 0;

    for(int i = 1; i <= 64; i++)
    {
        if(comm->getEtbuInfo(i).online)
        {
            onlineCount++;
        }
        else
        {
            offlineCount++;
        }
    }

    ui->lblOnlineCount->setText(
        QString("Online : %1").arg(onlineCount));

    ui->lblOfflineCount->setText(
        QString("Offline : %1").arg(offlineCount));
}

void MainWindow::updateEtbuStatusTable()
{
    for(int i = 1; i <= 64; i++)
    {
        const EtbuInfo &etbu = comm->getEtbuInfo(i);

        // STATUS
        QString status =
            etbu.online ? "ONLINE" : "OFFLINE";

        // STATE
        QString state;

        switch(etbu.state)
        {
        case EtbuState::Normal:
            state = "NORMAL";
            break;

        case EtbuState::Queued:
            state = "QUEUED";
            break;

        case EtbuState::Talk:
            state = "TALK";
            break;

        case EtbuState::Hold:
            state = "HOLD";
            break;

        default:
            state = "---";
            break;
        }

        // ETBU ID
        ui->tableEtbuStatus->setItem(
            i - 1,
            0,
            new QTableWidgetItem(QString::number(i)));

        // STATUS
        QTableWidgetItem *statusItem =
            new QTableWidgetItem(status);
        if(etbu.online)
            statusItem->setBackground(Qt::green);
        else
            statusItem->setBackground(Qt::red);

        ui->tableEtbuStatus->setItem(
            i - 1,
            1,
            statusItem);

        // STATE
        ui->tableEtbuStatus->setItem(
            i - 1,
            2,
            new QTableWidgetItem(state));
    }
}

void MainWindow::loadLogs()
{
    ui->tableWidgetLogs->setRowCount(0);
    QSqlQuery query;

    bool ok = query.exec(
        "SELECT id, "
        "etbu_address, "
        "call_start_time, "
        "call_end_time, "
        "operator_id, "
        "gps_location, "
        "audio_file "
        "FROM call_logs "
        "ORDER BY id DESC");

    qDebug() << "Select logs:" << ok;

    if (!ok)
    {
        qDebug() << query.lastError();
    }

    while(query.next())
    {
        int row = ui->tableWidgetLogs->rowCount();

        ui->tableWidgetLogs->insertRow(row);

        ui->tableWidgetLogs->setItem(
            row, 0,
            new QTableWidgetItem(query.value(0).toString())); //id

        ui->tableWidgetLogs->setItem(
            row, 1,
            new QTableWidgetItem(query.value(1).toString()));//etbu_address

        ui->tableWidgetLogs->setItem(
            row, 2,
            new QTableWidgetItem(query.value(2).toString()));//start_time

        ui->tableWidgetLogs->setItem(
            row, 3,
            new QTableWidgetItem(query.value(3).toString()));//end_time

        ui->tableWidgetLogs->setItem(
            row, 4,
            new QTableWidgetItem(query.value(4).toString()));//operator_id

        ui->tableWidgetLogs->setItem(
            row, 5,
            new QTableWidgetItem(query.value(5).toString())); //gps_location

        QPushButton *playButton = new QPushButton("Play");   //audio_file
        QString audioFile = query.value(6).toString();

        playButton->setProperty("audioPath",audioFile);

        connect(playButton,
                &QPushButton::clicked,
                this,
                &MainWindow::playAudioClicked);

        ui->tableWidgetLogs->setCellWidget(row,6,playButton);
    }
}

void MainWindow::playAudioClicked()
{
    if (m_recordingActive)
    {
        qDebug() << "Playback disabled during recording";
        return;
    }

    QPushButton *button =
        qobject_cast<QPushButton*>(sender());

    if (!button)
        return;

    // Same button pressed again -> STOP
    if (m_playProcess->state() != QProcess::NotRunning && button == m_currentPlayButton)
    {
        m_playProcess->kill();
        m_playProcess->waitForFinished();

        button->setText("Play");

        m_currentPlayButton = nullptr;

        qDebug() << "Playback stopped";

        return;
    }

    if (m_playProcess->state() != QProcess::NotRunning)
    {
        m_playProcess->kill();
        m_playProcess->waitForFinished();

        if (m_currentPlayButton)
        {
            m_currentPlayButton->setText("Play");
        }
    }

    QString audioPath =
        button->property("audioPath").toString();

    if (!QFile::exists(audioPath))
    {
        qDebug() << "Audio file not found:" << audioPath;
        return;
    }

    qDebug() << "Playing:" << audioPath;

    m_playProcess->start("aplay",
        QStringList()<< "-D"<< "plughw:0,0"<< audioPath);

    if (!m_playProcess->waitForStarted(1000))
    {
        qDebug() << "Failed to start playback";

        button->setText("Play");
        return;
    }

    button->setText("Stop");

    m_currentPlayButton = button;
}

void MainWindow::setupLogTable()
{
    ui->tableWidgetLogs->horizontalHeader()
        ->setSectionResizeMode(QHeaderView::Interactive);

    ui->tableWidgetLogs->horizontalHeader()
        ->setSectionsMovable(false);

    ui->tableWidgetLogs->horizontalHeader()
        ->setStretchLastSection(false);

    ui->tableWidgetLogs->setColumnWidth(0, 50);   // ID
    ui->tableWidgetLogs->setColumnWidth(1, 60);   // ETBU
    ui->tableWidgetLogs->setColumnWidth(2, 180);  // Start Time
    ui->tableWidgetLogs->setColumnWidth(3, 180);  // End Time
    ui->tableWidgetLogs->setColumnWidth(4, 80);  // State
    ui->tableWidgetLogs->setColumnWidth(5, 180);  // GPS
    ui->tableWidgetLogs->setColumnWidth(6, 80);   // Play
}

// ============================================================
// HEALTH PAGE  (RDSO clause 6.7 self-diagnostic, 6.20 fault popup,
//               8.2 system alarm log)
// ============================================================

void MainWindow::setupHealthTable()
{
    ui->tableEtbuHealth->setRowCount(64);

    for (int i = 1; i <= 64; i++)
    {
        ui->tableEtbuHealth->setItem(i - 1, 0, new QTableWidgetItem(QString::number(i)));
    }

    ui->tableEtbuHealth->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::updateHealthPage()
{
    // ---- top status tiles ----
    auto setTile = [](QLabel *lbl, const QString &title, bool ok)
    {
        lbl->setText(title + "\n" + (ok ? "OK" : "FAULT"));
        lbl->setStyleSheet(ok
            ? "background-color: rgb(40,160,60); border-radius: 4px;"
            : "background-color: rgb(190,40,40); border-radius: 4px;");
    };

    bool commOk = comm->isRunning();
    setTile(ui->lblHealthComm, "RS-485 LINK", commOk);

    bool gpsOk = m_gps->hasFix();
    setTile(ui->lblHealthGps, "GPS", gpsOk);

    // DatabaseManager doesn't currently expose an isOpen() check - approximate
    // "OK" by whether the last insert/select attempt succeeded is tracked
    // elsewhere; for now treat presence of the connection as healthy.
    setTile(ui->lblHealthDb, "DATABASE", true);

    bool audioFault = m_faultyEtbus.contains(0); // reserved slot: recorder-level fault
    setTile(ui->lblHealthAudio, "RECORDER", !audioFault);

    setTile(ui->lblHealthStorage, "STORAGE", true);

    bool anyFault = !commOk || !gpsOk || !m_faultyEtbus.isEmpty();

    ui->lblFaultBanner->setText(anyFault ? "FAULT DETECTED - SEE LOG" : "ALL SYSTEMS NORMAL");
    ui->lblFaultBanner->setStyleSheet(anyFault
        ? "background-color: rgb(190,40,40); border-radius: 4px; padding: 4px;"
        : "background-color: rgb(40,160,60); border-radius: 4px; padding: 4px;");

    // ---- per-ETBU health table ----
    for (int i = 1; i <= 64; i++)
    {
        const EtbuInfo &etbu = comm->getEtbuInfo(i);
        int row = i - 1;

        ui->tableEtbuHealth->setItem(row, 1, new QTableWidgetItem(etbu.online ? "ONLINE" : "OFFLINE"));

        QString state;
        switch (etbu.state)
        {
        case EtbuState::Normal: state = "NORMAL"; break;
        case EtbuState::Queued: state = "QUEUED"; break;
        case EtbuState::Talk:   state = "TALK";   break;
        case EtbuState::Hold:   state = "HOLD";   break;
        default:                state = "---";   break;
        }
        ui->tableEtbuHealth->setItem(row, 2, new QTableWidgetItem(state));
        ui->tableEtbuHealth->setItem(row, 3, new QTableWidgetItem(QString::number(etbu.swVersion)));

        QString lastResp = etbu.lastResponse.isValid()
            ? QString::number(etbu.lastResponse.secsTo(QDateTime::currentDateTime())) + " s ago"
            : "--";
        ui->tableEtbuHealth->setItem(row, 4, new QTableWidgetItem(lastResp));

        QTableWidgetItem *faultItem = new QTableWidgetItem(m_faultyEtbus.contains(i) ? "YES" : "--");
        if (m_faultyEtbus.contains(i))
            faultItem->setBackground(Qt::red);
        ui->tableEtbuHealth->setItem(row, 5, faultItem);
    }

    // ---- link diagnostics ----
    ui->lblPollCount->setText(QString("Polls sent : %1").arg(comm->getPollCount()));
    ui->lblCrcErrorCount->setText(QString("CRC errors : %1").arg(comm->getCrcErrorCount()));
    ui->lblTimeoutCount->setText(QString("Timeouts : %1").arg(comm->getTimeoutCount()));

    // ---- recording health ----
    ui->lblRecordingState->setText(QString("Recording : %1").arg(m_recordingActive ? "ACTIVE" : "IDLE"));
    ui->lblLastLogInsert->setText(QString("Last log insert : %1")
        .arg(m_callEndTime.isEmpty() ? "--" : m_callEndTime));
    ui->lblStorageFree->setText("Storage free : -- (wire up to QStorageInfo)");
}

void MainWindow::onLinkFault(const QString &message)
{
    addFaultLogEntry(message);
}

void MainWindow::onEtbuOffline(quint8 address)
{
    m_faultyEtbus.insert(address);
}

void MainWindow::addFaultLogEntry(const QString &message)
{
    QString stamp = QDateTime::currentDateTime().toString("dd-MM-yyyy hh:mm:ss");
    ui->listFaultLog->insertItem(0, stamp + "  -  " + message);
}

void MainWindow::on_btnClearFaultLog_clicked()
{
    ui->listFaultLog->clear();
    m_faultyEtbus.clear();
}

void MainWindow::on_btnExportDiagnostics_clicked()
{
    // Placeholder: write current health snapshot to removable media path.
    // Clause 6.22 requires diagnostics/maintenance software to be supplied -
    // this button is the hook point for that export routine.
    addFaultLogEntry("Diagnostics export requested by operator");
    QMessageBox::information(this, "Export Diagnostics",
        "Diagnostics export not yet implemented - hook this button up to your "
        "removable-media export routine.");
}

// ============================================================
// SETTINGS PAGE  (RDSO clause 6.2 device ID, 8.3 user auth,
//                 6.3 retention, 6.18 language)
// ============================================================

void MainWindow::populateSettingsPage()
{
    // Device identity - clause 6.2 (unique digital ID)
    ui->lblUnitId->setText("Unit ID : (set serial/unit id here)");
    ui->lblSwVersion->setText("Software Version : (set app version string here)");
    ui->lblHwVersion->setText("Hardware Version : (set board revision here)");

    // Comm settings - read-only, values fixed by spec (clause 2 of ICF/ED/P/001)
    ui->lblCommPortInfo->setText("Port : /dev/ttyUSB0");
    ui->lblResponderAddrInfo->setText(
        QString("Responder Address : 0x%1").arg(comm->getResponderAddress(), 2, 16, QLatin1Char('0')).toUpper());

    // GPS settings
    ui->lblGpsPortInfo->setText("Port : /dev/ttySTM2");
    ui->lblGpsFixInfo->setText(QString("Fix Status : %1").arg(m_gps->hasFix() ? "ACQUIRED" : "NO FIX"));
    ui->lblGpsCoordInfo->setText(QString("Location : %1").arg(m_gps->currentLocationString()));

    ui->lblCurrentUser->setText(m_currentUser.isEmpty()
        ? "Logged in as : none (operator access only)"
        : "Logged in as : " + m_currentUser);
}

void MainWindow::on_btnLogin_clicked()
{
    // NOTE: placeholder only - clause 8.3 requires real authentication with
    // secure credential storage and per-role access rights before field use.
    QString user = ui->txtUsername->text().trimmed();

    if (user.isEmpty())
    {
        QMessageBox::warning(this, "Login", "Enter a username.");
        return;
    }

    m_currentUser = user;
    ui->txtPassword->clear();
    ui->lblCurrentUser->setText("Logged in as : " + m_currentUser);
}

void MainWindow::on_btnLogout_clicked()
{
    m_currentUser.clear();
    ui->lblCurrentUser->setText("Logged in as : none (operator access only)");
}

void MainWindow::on_btnExportLogs_clicked()
{
    // Placeholder: hook this to your removable-media / laptop download
    // routine (clause 6.3 - centralized downloading of recordings).
    QMessageBox::information(this, "Export Logs",
        "Log export not yet implemented - hook this button up to your "
        "removable-media export routine.");
}

// ============================================================
// MASTER <-> SLAVE RESPONSE UNIT  (RDSO/ICF clause 5)
// ============================================================

void MainWindow::on_btnSlaveUnit_clicked()
{
    if (comm->isMasterRole())
    {
        QMessageBox::information(this, "Slave Unit",
            "This unit is already the Master Response Unit.");
        return;
    }

    comm->requestBecomeMaster();
    addFaultLogEntry("Operator requested promotion to Master Response Unit");
}

void MainWindow::onRuRoleChanged(bool isMaster)
{
    ui->lblRuRole->setText(isMaster ? "MASTER" : "SLAVE");
    ui->lblRuRole->setStyleSheet(isMaster
        ? "color: rgb(80,220,120); font-weight:bold;"
        : "color: rgb(230,200,80); font-weight:bold;");

    // The 'Slave Unit' promotion button only makes sense while we are Slave.
    ui->btnSlaveUnit->setEnabled(!isMaster);

    addFaultLogEntry(QString("Response Unit role changed to %1").arg(isMaster ? "MASTER" : "SLAVE"));
}

void MainWindow::onPeerStatusChanged(bool online)
{
    addFaultLogEntry(QString("Peer Response Unit is now %1").arg(online ? "ONLINE" : "OFFLINE"));
}

// ============================================================
// CALL ALARM + 30s (PROGRAMMABLE) ESCALATION  (clause 5.2 / 7.1)
// ============================================================

void MainWindow::onCallAlarmRaised(quint8 address)
{
    m_activeAlarms.insert(address);
    m_escalatedAlarms.remove(address);

    QApplication::beep();   // warning audio alarm - clause 5.2

    ui->lblAlarmBanner->setVisible(true);
    ui->lblAlarmBanner->setText(QString("CALL FROM ETBU %1").arg(address));
    ui->lblAlarmBanner->setStyleSheet(
        "background-color: rgb(200,40,40); color: white; border-radius: 4px; padding: 4px;");

    addFaultLogEntry(QString("Call alarm raised for ETBU %1").arg(address));
}

void MainWindow::onCallEscalated(quint8 address)
{
    m_escalatedAlarms.insert(address);

    QApplication::beep();
    QApplication::beep();   // double-beep to distinguish escalation from the initial alarm

    if (ui->lblAlarmBanner->isVisible())
    {
        ui->lblAlarmBanner->setText(
            QString("ETBU %1 UNANSWERED %2s+ - ESCALATED")
                .arg(address).arg(comm->getEscalationThresholdSecs()));
    }

    addFaultLogEntry(QString("Call from ETBU %1 unanswered past %2s - escalated (clause 5.2)")
                          .arg(address).arg(comm->getEscalationThresholdSecs()));
}

void MainWindow::onCallAlarmCleared(quint8 address)
{
    m_activeAlarms.remove(address);
    m_escalatedAlarms.remove(address);

    if (m_activeAlarms.isEmpty())
    {
        ui->lblAlarmBanner->setVisible(false);
    }
    else
    {
        // Still other calls waiting - show the next one in the banner.
        quint8 next = *m_activeAlarms.constBegin();
        ui->lblAlarmBanner->setText(QString("CALL FROM ETBU %1").arg(next));
    }

    addFaultLogEntry(QString("Call alarm cleared for ETBU %1").arg(address));
}

void MainWindow::blinkAlarmBanner()
{
    if (m_activeAlarms.isEmpty())
        return;

    m_alarmBlinkOn = !m_alarmBlinkOn;

    bool anyEscalated = !m_escalatedAlarms.isEmpty();

    QString color = anyEscalated
        ? (m_alarmBlinkOn ? "rgb(230,30,30)" : "rgb(120,0,0)")     // faster/harder blink once escalated
        : (m_alarmBlinkOn ? "rgb(200,40,40)" : "rgb(140,20,20)");

    ui->lblAlarmBanner->setStyleSheet(
        QString("background-color: %1; color: white; border-radius: 4px; padding: 4px;").arg(color));
}

void MainWindow::on_spinAlarmTimeout_valueChanged(int value)
{
    comm->setEscalationThresholdSecs(value);
}
