#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "master_communication.h"
#include "audio/audiorecorder.h"
#include "databasemanager.h"
#include <QProcess>
#include <QPushButton>
#include <QFile>
#include <QSet>
#include "gpsmanager.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void showSettingsPage();
    void ShowDashboardpage();
    void showHealthPage();
    void showLogPage();
    void updateDateTime();
    void on_btnComm_clicked();
    void updateQueueList(const QList<quint8> &queue);
    void onQueueItemSelected(int row, int column);
    void updateActiveCall(quint8 address,const QString &state);
    void on_btnanswer_clicked();
    void on_btnEnd_clicked();
    void on_btnHold_clicked();
    void updateEtbuCounts();
    void updateEtbuStatusTable();
    void playAudioClicked();

    // Health page (RDSO clause 6.7 / 6.20 / 8.2)
    void updateHealthPage();
    void onLinkFault(const QString &message);
    void onEtbuOffline(quint8 address);
    void on_btnClearFaultLog_clicked();
    void on_btnExportDiagnostics_clicked();

    // Settings page (RDSO clause 6.2 / 8.3 / 6.3 / 6.18)
    void populateSettingsPage();
    void on_btnLogin_clicked();
    void on_btnLogout_clicked();
    void on_btnExportLogs_clicked();

    // Master<->Slave Response Unit (clause 5)
    void on_btnSlaveUnit_clicked();
    void onRuRoleChanged(bool isMaster);
    void onPeerStatusChanged(bool online);

    // Clause 5.2/7.1 - call alarm + 30s (programmable) escalation
    void onCallAlarmRaised(quint8 address);
    void onCallEscalated(quint8 address);
    void onCallAlarmCleared(quint8 address);
    void blinkAlarmBanner();
    void on_spinAlarmTimeout_valueChanged(int value);

private:
    void setIdleState();
    void setTalkState();
    void loadLogs();
    void setupLogTable();
    void setupHealthTable();
    void addFaultLogEntry(const QString &message);
    //void loadLogs();

    Ui::MainWindow *ui;
    QTimer *clockTimer;
    QTimer *statusTimer;

    // Single Modbus engine (owns serial port internally)
    Master_communication *comm;
    quint8 selectedEtbu = 0;
    AudioRecorder *m_audioRecorder = nullptr;

    bool m_recordingActive = false;
    DatabaseManager m_database;

    QString m_callStartTime;
    QString m_callEndTime;
    QString m_currentAudioFile;
    uint8_t m_currentEtbuAddress = 0;

    QProcess *m_playProcess = nullptr;
    QPushButton *m_currentPlayButton = nullptr;

    GpsManager *m_gps = nullptr;
    QString m_callStartLocation = "--";

    // Health page state
    QSet<quint8> m_faultyEtbus;      // ETBUs currently flagged (clause 6.20 popup blink)

    // Settings page state - NOTE: this is a basic username check only, not a
    // hardened auth implementation. Clause 8.3 requires real user/role
    // management with secure credential storage before field deployment.
    QString m_currentUser;

    // Clause 5.2/7.1 - call alarm + visual location indication state
    QSet<quint8> m_activeAlarms;      // ETBUs currently ringing (queued, unanswered)
    QSet<quint8> m_escalatedAlarms;   // subset that has passed the 30s (programmable) timeout
    QTimer *alarmBlinkTimer = nullptr;
    bool m_alarmBlinkOn = false;
};
#endif // MAINWINDOW_H
