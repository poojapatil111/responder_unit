#ifndef MASTER_COMMUNICATION_H
#define MASTER_COMMUNICATION_H

#include <QSerialPort>
#include <QTimer>
#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QSet>

enum class EtbuState
{
    Offline,
    Normal,
    Queued,
    Talk,
    Hold,
    Unknown
};

struct EtbuInfo
{
    quint8 address = 0;
    bool online = false;
    quint8 swVersion = 0;
    EtbuState state = EtbuState ::Offline;
    quint8 lastFunctionCode = 0x00;
    QDateTime lastResponse;
};

class Master_communication :public QObject
{
    Q_OBJECT

public:
    explicit Master_communication(QObject *parent = nullptr);
    void startCommunication();
    void stopCommunication();
    bool isRunning() const;
    bool isCallOnHold() const;
    bool isCallActive() const;
    bool validateFrame(const QByteArray &frame);
    void processFunctionCode(quint8 address,quint8 functionCode,quint8 swVersion);
    void answerCall(quint8 address);
    void endCall();
    void holdCall();
    void resumeCall();
    const EtbuInfo& getEtbuInfo(int address) const;

    // Link diagnostics - used by the Health page (RDSO clause 6.7 / 8.2)
    quint32 getPollCount() const { return pollCount; }
    quint32 getCrcErrorCount() const { return crcErrorCount; }
    quint32 getTimeoutCount() const { return timeoutCount; }
    QString getLastFrameString() const { return lastFrameString; }
    quint8 getResponderAddress() const { return responderAddress; }

    // ---- Section 5: Master <-> Slave Response Unit protocol ----
    bool isMasterRole() const { return m_isMaster; }
    bool isPeerOnline() const { return m_peerOnline; }

    // Called when the operator presses the on-screen 'Slave Unit' button
    // (clause 5.3.1) - a Slave sends an Active Request to ask to become
    // Master. Has no effect if this unit is already Master.
    //Added by pooja on 1 august 2026
    // Master. Has no effect if this unit is already Master, OR if a
    // request was already sent and we are still waiting for the current
    // Master's 0x8F reply (prevents duplicate 0x8D transmissions from a
    // double-click / event-queue race - see requestBecomeMaster()).

    void requestBecomeMaster();

    // ---- Clause 5.2 / 7.1: call alarm + 30-second (programmable) escalation ----
    int getEscalationThresholdSecs() const { return m_escalationThresholdSecs; }
    void setEscalationThresholdSecs(int seconds) { m_escalationThresholdSecs = seconds > 0 ? seconds : 30; }

private slots:
    void pollDevice();
    void readResponse();
    void onTimeout();
    void pollPeerResponseUnit();   // periodic health poll to the other Response Unit
    void checkCallEscalation();    // clause 5.2 - 30s (programmable) escalation check
    void onMasterRequestTimeout(); // Added by pooja on 1 august 2026 no 0x8F reply arrived after requestBecomeMaster() - clear the in-flight guard
    void onMasterSilenceTimeout(); // Added by pooja on 3 august 2026 Slave-side: Master hasn't polled (0x8E) for 30s - auto-promote (failover)
signals:
    void callQueueUpdated(const QList<quint8> &queue);
    void activeCallChanged(quint8 address,QString state);
    void linkFault(const QString &message);          // for Health page fault/alarm log (clause 8.2)
    void etbuOffline(quint8 address);                 // for Health page fault flag (clause 6.7/6.20)
    void roleChanged(bool isMaster);                  // UI updates the 'Slave Unit' button / master banner
    void peerStatusChanged(bool online);               // UI shows peer Response Unit health
    void masterRequestFailed();                     //Added by pooja on 1 augiust 2026 requestBecomeMaster() timed out with no 0x8F reply - UI should re-enable the button

    // Clause 5.2 / 7.1 - warning audio alarm + visual location indication
    void callAlarmRaised(quint8 address);      // a new ETBU call just entered the queue
    void callEscalated(quint8 address);        // unanswered past the (programmable) 30s timeout
    void callAlarmCleared(quint8 address);     // call answered or ended - stop alarming for it


private:
    QString frameToString(const QByteArray &frame);
    EtbuState decodeState(quint8 functionCode);
    //Commented by pooja on 30 jul 2026 //void handleResponseUnitFrame(quint8 srcAddress, quint8 functionCode);
    void handleResponseUnitFrame(quint8 srcAddress,
                                 quint8 destAddress,
                                 quint8 functionCode);// Added by pooja on 30 july 2026 for RU frame
    //static constexpr quint8 responderAddress = 0x01;  //commented by pooja on 31 july 2026 for "means Response Unit always has address 0x01, even when it is acting as a Slave."
    quint8 responderAddress = SLAVE_RU_ADDRESS;         //Added by pooja on 31 july 2026 for The address must change when the role changes.
    static constexpr quint8 etbuAddress = 0x0A;

    // NOTE ON ADDRESSING: RDSO/ICF spec ICF/ED/P/001 clause 5 states in
    // plain text that "when a response unit is working as a Master... it
    // uses address 0x01. When... Slave... it uses address 0x02" - the OCR'd
    // table cells in the same document show a garbled "0x99+1"/"0x99+2"
    // which does not parse as a usable byte value, so these two constants
    // follow the unambiguous text instead. Confirm against your original
    // (non-OCR) spec copy and change these two lines if it differs.
    static constexpr quint8 MASTER_RU_ADDRESS = 0x01;
    static constexpr quint8 SLAVE_RU_ADDRESS  = 0x02;

    void handlePollResponse(quint8 address);

    void handleCallRequest(quint8 address);

    void handleTalk(quint8 address);

    void handleHold(quint8 address);

    QByteArray createFrameForState(quint8 address);
    void checkEtbuoffline();

    QSerialPort *serial;
    QTimer *pollTimer;          // Sends poll every second
    QTimer *timeoutTimer;       // Waits for ETBU response
    quint8 activeEtbu = 255;
    QByteArray rxBuffer;
    bool callActive = false;
    bool callOnHold = false;
    EtbuInfo etbuArray[65];
    QList<quint8> callQueue;
    QDateTime systemStartTime;
    quint8 currentPoll = 1;   // ← start from 1, change qint8 to quint8

    // Link diagnostics counters - Health page (clause 6.7/8.2)
    quint32 pollCount = 0;
    quint32 crcErrorCount = 0;
    quint32 timeoutCount = 0;
    QString lastFrameString;

    // Section 5 - Master/Slave Response Unit peer state
    //bool m_isMaster = true;               // this unit starts as Master; flips on handshake
    bool m_isMaster = false;                //Added by pooja on 30 july 2026 for response unit bydefault slave unit
    bool m_peerOnline = false;
    QDateTime m_peerLastResponse;
    QTimer *peerPollTimer = nullptr;     // Master polls Slave periodically (clause 5.3.2)

    // Added by pooja on 1 august 2026 Guards against a second 0x8D being sent while a promotion request is

    // Added by pooja on 3 august 2026 Slave-side failover watchdog: normally the Master sends an 0x8E
    // health poll to the Slave once every second (peerPollTimer on the
    // Master's side). If this unit is Slave and does NOT see an 0x8E
    // addressed to it for MASTER_SILENCE_TIMEOUT_MS, the Master is presumed
    // dead/unreachable, and this unit promotes itself to Master directly
    // (no 0x8D/0x8F handshake possible since the Master isn't answering).
    //
    // m_masterEverSeen only arms the watchdog after the FIRST genuine 0x8E
    // is received. This matters at cold boot: both units may default to
    // Slave with no Master on the bus yet (see bootstrap discussion) - if
    // the watchdog were armed immediately, both units would independently
    // time out after 30s and self-promote at the same time, causing an
    // address collision (both at 0x01). Arming only after a real Master has
    // been observed at least once means this watchdog only fires for a
    // genuine failure of an already-established Master.
    QTimer *masterWatchdogTimer = nullptr;
    bool m_masterEverSeen = false;
    static constexpr int MASTER_SILENCE_TIMEOUT_MS = 30000;   // 30s per requirement

    // Guards against a second 0x8D being sent while a promotion request is
    //Added done by pooja on 3 august 2026

    // already pending (double-click on 'Slave Unit', or the button-click
    // event racing the 0x8F reply's readyRead() event in the Qt event
    // queue). Set true in requestBecomeMaster(), cleared as soon as we
    // actually become Master (0x8F received) or if masterRequestTimer
    // expires with no reply.
    bool m_masterRequestInFlight = false;
    QTimer *masterRequestTimer = nullptr;   // single-shot; clears m_masterRequestInFlight if no 0x8F reply arrives
    // Added done by pooja on 1 august 2026

    // Clause 5.2 / 7.1 - call alarm + 30s (programmable) escalation
    QMap<quint8, QDateTime> m_callQueuedSince;
    QSet<quint8> m_escalatedCalls;
    int m_escalationThresholdSecs = 30;   // "(programmable)" per spec text
    QTimer *escalationTimer = nullptr;    // checks queued calls once a second

};


#endif // MASTER_COMMUNICATION_H
