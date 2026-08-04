#include "master_communication.h"
#include "master_frames.h"
#include <QDebug>

Master_communication::Master_communication(QObject *parent):QObject(parent)
{
    serial = new QSerialPort(this);

    // Serial configuration
    //serial->setPortName("/dev/tnt0");
    serial->setPortName("/dev/ttyUSB0");//commented by pooja for test responder unit master<->slave
    //serial->setPortName("/tmp/ttyA");
    //serial->setBaudRate(QSerialPort::Baud9600);
    serial->setBaudRate(QSerialPort::Baud38400);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    systemStartTime = QDateTime::currentDateTime();
    for(int i = 1; i <= 64; i++)
    {
        etbuArray[i].address = i;
        etbuArray[i].online = false;
        //etbuArray[i].state = EtbuState::Offline;
    }

    pollTimer = new QTimer(this);                               //Poll Timer
    timeoutTimer = new QTimer(this);                            //timeouttimer
    timeoutTimer->setSingleShot(true);
    peerPollTimer = new QTimer(this);                           //Section 5 peer health poll
    escalationTimer = new QTimer(this);                         //Clause 5.2 - 30s call escalation check

    masterRequestTimer = new QTimer(this);                      // Added by pooja on 1 august 2026 guards requestBecomeMaster() against no-reply
    masterRequestTimer->setSingleShot(true);                    // Added by pooja on 1 august 2026

    masterWatchdogTimer = new QTimer(this);                     // Added by pooja on 3 August 2026 Slave-side: detects Master going silent (30s failover)
    masterWatchdogTimer->setSingleShot(true);                   // Added by pooja on 3 August 2026 Slave-side: detects Master going silent (30s failover)

    connect(pollTimer, &QTimer::timeout,this, &Master_communication::pollDevice);
    connect(serial,&QSerialPort::readyRead,this,&Master_communication::readResponse); // Receive Data
    connect(timeoutTimer,&QTimer::timeout,this,&Master_communication::onTimeout);
    connect(peerPollTimer, &QTimer::timeout, this, &Master_communication::pollPeerResponseUnit);
    connect(escalationTimer, &QTimer::timeout, this, &Master_communication::checkCallEscalation);
    connect(masterRequestTimer, &QTimer::timeout, this, &Master_communication::onMasterRequestTimeout);     //Added by pooja on 1 august 2026
    connect(masterWatchdogTimer, &QTimer::timeout, this, &Master_communication::onMasterSilenceTimeout);    // Added by pooja on 3 August 2026 Slave-side: detects Master going silent (30s failover)
    escalationTimer->start(1000);   // check once a second regardless of Master/Slave role
}

//sending polling string
void Master_communication::pollDevice()
{
    //Added by pooja on 30 july 2026 for RU bydefault slave unit
    if(!m_isMaster)
        return;
    //Added done by pooja on 30 july 2026 for RU bydefault slave unit

    checkEtbuoffline();             // Check ETBU Health

    quint8 etbuAddress = currentPoll;
    currentPoll++;

    if(currentPoll > 65)
    {
        currentPoll = 1;
    }

    QByteArray frame = createFrameForState(etbuAddress);

    serial->write(frame);
    //Added by pooja on 16 jul 2026
    serial->flush();
    serial->waitForBytesWritten(100);
    //added done by pooja

    qDebug()<< "TX ETBU"<< etbuAddress<< ":"<< frameToString(frame);

    pollCount++;
    lastFrameString = "TX ETBU " + QString::number(etbuAddress) + " : " + frameToString(frame);

    timeoutTimer->start(1000);
}

//read ETB response
void Master_communication::readResponse()
{
    qDebug() << "readyRead() triggered";
    // Append newly received bytes
    rxBuffer.append(serial->readAll());

    qDebug() << "Received bytes:" << rxBuffer.toHex(' ');

    // process all complete frames available in buffer
    while(rxBuffer.size() >= 6)
    {
        //startbyte
        int startIdx = -1;

        for(int i = 0; i < rxBuffer.size();i++)
        {
            if(static_cast<quint8>(rxBuffer[i]) == 0xFF)
            {
                startIdx = i;
                break;
            }
        }
        //nostartbyte found
        if(startIdx == -1)
        {
            qDebug() << "No start byte found";
            rxBuffer.clear();
            break;
        }
        //remove garbage before start byte
        if(startIdx > 0)
        {
            qDebug() << "Discarding" << startIdx << "garbage bytes";
            rxBuffer.remove(0,startIdx);
        }
        //wait for complete frame
        if(rxBuffer.size() < 6)   break;
        //extract 6-byte frame
        QByteArray frame = rxBuffer.left(6);
        //validate frame
        if(validateFrame(frame))
        {
            timeoutTimer->stop();
            //remove processed frame
            rxBuffer.remove(0,6);
            //convert frame to string
            QString rxString;
            for(unsigned char byte :frame)
            {
                rxString +=QString("%1 ").arg(byte,2,16,QLatin1Char('0')).toUpper();
            }

            qDebug() << "RX :" << rxString;
            lastFrameString = "RX : " + rxString;

            //commented by pooja on 30 july 2026
            // //decode frame
            // qint8 address = static_cast<quint8>(frame[1]);
            // quint8 functionCode = static_cast<quint8>(frame[2]);
            // quint8 swVersion = static_cast<quint8>(frame[3]);
            //commented done by pooja on 30 july 2026

            //Added by pooja on 30 july 2026
            quint8 byte1 = static_cast<quint8>(frame[1]);
            quint8 byte2 = static_cast<quint8>(frame[2]);
            quint8 byte3 = static_cast<quint8>(frame[3]);
            //Added done by pooja on 30 july 2026


            // Section 5 traffic (Master<->Slave Response Unit) uses function
            // codes 0x8D/0x8E/0x8F, which never overlap with the 0x00-0x03
            // ETBU codes, so we can route purely on function code. This must
            // be checked BEFORE touching etbuArray[], since a peer Response
            // Unit's address (0x01/0x02) would otherwise be misinterpreted
            // as ETBU #1/#2 and corrupt that entry.

            //Commented by pooja on 30 jul 2026
            // if (functionCode == 0x8D || functionCode == 0x8E || functionCode == 0x8F)
            // {
            //     qDebug() << "Address      :" << address;
            //     qDebug() << "Function Code:" << functionCode;

            //     handleResponseUnitFrame(address, functionCode);
            // }
            // else
            // {
            //     // update ETBU database
            //     EtbuInfo &etbu = etbuArray[address];

            //     bool wasOffline = !etbu.online;

            //     etbu.address = address;
            //     etbu.online = true;
            //     etbu.swVersion = swVersion;
            //     etbu.lastFunctionCode = functionCode;
            //     etbu.lastResponse = QDateTime::currentDateTime();

            //     qDebug() << "Address      :" << address;
            //     qDebug() << "Function Code:" << functionCode;
            //     qDebug() << "SW Version   :" << swVersion;

            //     // ETBU Recovered
            //     if(wasOffline)
            //     {
            //         qDebug() << "ETBU" << address<< "ONLINE";
            //     }

            //     // Handle Function Code
            //     processFunctionCode(address,functionCode,swVersion);
            // }
            //Commented done by pooja on 30 jul 2026


            //Added by pooja on 30 jul 2026
            if (byte3 == 0x8D || byte3 == 0x8E || byte3 == 0x8F)
            {
                // Response Unit frame
                quint8 srcAddress  = byte1;
                quint8 destAddress = byte2;
                quint8 function    = byte3;

                handleResponseUnitFrame(srcAddress,
                                        destAddress,
                                        function);
            }
            else
            {
                // ETBU frame
                quint8 address      = byte1;
                quint8 functionCode = byte2;
                quint8 swVersion    = byte3;

                EtbuInfo &etbu = etbuArray[address];

                bool wasOffline = !etbu.online;

                etbu.address = address;
                etbu.online = true;
                etbu.swVersion = swVersion;
                etbu.lastFunctionCode = functionCode;
                etbu.lastResponse = QDateTime::currentDateTime();

                qDebug() << "Address      :" << address;
                qDebug() << "Function Code:" << functionCode;
                qDebug() << "SW Version   :" << swVersion;

                // ETBU Recovered
                if(wasOffline)
                {
                    qDebug() << "ETBU" << address<< "ONLINE";
                }

                // Handle Function Code
                processFunctionCode(address,functionCode,swVersion);
               }
            //Added done by pooja on 30 jul 2026

        }
        else
        {
            // CRC Error
            qDebug() << "CRC ERROR";

            QString badFrame;

            for(unsigned char byte : frame)
            {
                badFrame += QString("%1 ")
                                .arg(byte,
                                     2,16,QLatin1Char('0')).toUpper();
            }

            qDebug() << "Bad Frame:" << badFrame;

            crcErrorCount++;
            emit linkFault(QString("CRC error on frame: %1").arg(badFrame));

            // Shift by one byte and resync
            rxBuffer.remove(0, 1);
        }
    }
}

// Validate Received Frame
bool Master_communication::validateFrame(const QByteArray &frame)
{
    if (frame.size() != 6)
        return false;

    // Check Start Byte
    if (static_cast<quint8>(frame[0]) != 0xFF)
        return false;

    quint16 receivedCRC =
        (static_cast<quint8>(frame[4]) << 8) |
        static_cast<quint8>(frame[5]);

    QByteArray crcData = frame.left(4);

    quint16 calculatedCRC = master_frames::calculateCRC(crcData);

    return (receivedCRC == calculatedCRC);
}

EtbuState Master_communication::decodeState(quint8 functionCode)
{
    switch(functionCode)
    {
    case 0x00:
        return EtbuState::Normal;

    case 0x01:
        return EtbuState::Queued;

    case 0x02:
        return EtbuState::Talk;

    case 0x03:
        return EtbuState::Hold;

    default:
        return EtbuState::Unknown;
    }
}

void Master_communication::processFunctionCode(quint8 address,quint8 functionCode,quint8 swVersion)
{
    Q_UNUSED(swVersion);

    switch(functionCode)
    {
        case 0x00:
            handlePollResponse(address);
            break;

        case 0x01:
            handleCallRequest(address);
            break;

        case 0x02:
            handleTalk(address);
            break;

        case 0x03:
            handleHold(address);
            break;

        default:
            qDebug() << "UNKOWN FUNCTION:";
            break;
    }
}
void Master_communication::handlePollResponse(quint8 address)
{
    if (address >= 65)
        return; // safety check

    EtbuInfo &etbu = etbuArray[address];

    etbu.state = EtbuState::Normal;
    etbu.online = true;
    etbu.lastResponse = QDateTime::currentDateTime();

    //emit dataReceived(QString("ETBU %1 : NORMAL").arg(address));
    qDebug() << QString("ETBU %1 : NORMAL").arg(address);
}

void Master_communication::handleCallRequest(quint8 address)
{
    //Added by pooja on 30 july 2026 for RU by default slave unit
    if(m_isMaster)
    {
        qDebug()<<"MASTER received call.";
    }
    else
    {
        qDebug()<<"SLAVE monitoring call.";
    }
    //Added done by pooja on 30 july 2026 for RU by default slave unit


    qDebug() << QString("ETBU %1 : CALL REQUEST").arg(address);
    qDebug() <<"Passenger requesting communication";

    if (!callQueue.contains(address))
    {
        callQueue.append(address);
        emit callQueueUpdated(callQueue);

        qDebug() << "Queue Size:" << callQueue.size();

        // Clause 5.2/7.1: warning audio alarm + visual location indication
        // fires the moment a call enters the queue (not just on the active
        // call), and starts the clock for the 30s (programmable) escalation.
        m_callQueuedSince[address] = QDateTime::currentDateTime();
        m_escalatedCalls.remove(address);
        emit callAlarmRaised(address);
    }

    qDebug() << "Current Queue:";

    EtbuInfo &etbu = etbuArray[address];
    etbu.state = EtbuState::Queued;
}

void Master_communication::handleTalk(quint8 address)
{
    EtbuInfo &etbu = etbuArray[address];

    etbu.state = EtbuState::Talk;

    //emit dataReceived(QString("ETBU %1 : TALK MODE").arg(address));
}

void Master_communication::handleHold(quint8 address)
{
    EtbuInfo &etbu = etbuArray[address];

    etbu.state = EtbuState::Hold;

    //emit dataReceived(QString("ETBU %1 : HOLD").arg(address));
}

//commented by pooja on 20 july 2026 for to solve the communication problem
// void Master_communication::answerCall(quint8 address)
// {
//     if (address >= 65)
//         return;

//     EtbuInfo &etbu = etbuArray[address];
//     // Update state
//     etbu.state = EtbuState::Talk;

//     activeEtbu = address;
//     callActive = true;

//     // Remove from queue
//     callQueue.removeAll(address);

//     emit callQueueUpdated(callQueue);
//     emit activeCallChanged(address,"CALL ACTIVE");

// }
//commented done by pooja on 20 july 2026 for to solve the communication problem

//Added by pooja on 20 july 2026 for to solve problem of answerCall() never sends the TALK frame immediately
void Master_communication::answerCall(quint8 address)
{
    //Added by pooja on 30 july 2026 for RU bydefalu slave unit
    if(!m_isMaster)
    {
        qDebug()<<"Slave cannot answer ETBU call.";
        return;
    }
    //Added done by pooja on 30 july 2026 for RU bydefalu slave unit

    if(address >= 65)
        return;

    // Safety check
    if(!callQueue.contains(address))
    {
        qDebug() << "ETBU" << address << "is not present in Call Queue.";
        return;
    }


    EtbuInfo &etbu = etbuArray[address];

    etbu.state = EtbuState::Talk;

    activeEtbu = address;
    callActive = true;

    callQueue.removeAll(address);

    // Send TALK frame immediately
    QByteArray frame =
        master_frames::createTalkResponse(responderAddress, address);

    serial->write(frame);
    serial->flush();
    serial->waitForBytesWritten(100);

    qDebug() << "Talk Response:" << frameToString(frame);

    emit callQueueUpdated(callQueue);
    emit activeCallChanged(address, "CALL ACTIVE");

    // Clause 5.2 - alarm/escalation stops once the guard picks up the call
    m_callQueuedSince.remove(address);
    m_escalatedCalls.remove(address);
    emit callAlarmCleared(address);
}
//Added done by pooja on 20 july 2026 for to solve problem of answerCall() never sends the TALK frame immediately

void Master_communication::endCall()
{
    //Added by pooja on 30 july 2026 for RU bydefalu slave unit
    if(!m_isMaster)
    {
        qDebug()<<"Slave cannot End ETBU call.";
        return;
    }
    //Added done by pooja on 30 july 2026 for RU bydefalu slave unit

    if (!callActive) return;

    // ← Send end call frame to ETBU first!
    QByteArray frame = master_frames::createEndCallResponse(responderAddress, activeEtbu);

    serial->write(frame);
    //added by pooja on 16 jul 2026
    serial->flush();
    serial->waitForBytesWritten(100);
    //added done by poooja

    qDebug() << "End Call Frame:" << frameToString(frame);

    EtbuInfo &etbu = etbuArray[activeEtbu];
    etbu.state = EtbuState::Normal;

    // Clause 5.2 - make sure the alarm/escalation state is cleared for this
    // ETBU even if endCall() is reached without answerCall() having run.
    m_callQueuedSince.remove(activeEtbu);
    m_escalatedCalls.remove(activeEtbu);
    emit callAlarmCleared(activeEtbu);

    callActive = false;
    callOnHold = false;
    activeEtbu = 255;

    emit activeCallChanged(255, "NO ACTIVE CALL");
}

void Master_communication::holdCall()
{
    //Added by pooja on 30 july 2026 for RU bydefalu slave unit
    if(!m_isMaster)
    {
        qDebug()<<"Slave cannot hold ETBU call.";
        return;
    }
    //Added done by pooja on 30 july 2026 for RU bydefalu slave unit

    if (!callActive)
        return;

    QByteArray frame = master_frames::createHoldResponse(responderAddress,activeEtbu);

    serial->write(frame);
    //added by pooja on 16 jul 2026
    serial->flush();
    serial->waitForBytesWritten(100);
    //added done by pooja

    qDebug() << "Hold Response:" <<  frameToString(frame);

    EtbuInfo &etbu = etbuArray[activeEtbu];

    etbu.state = EtbuState::Hold;

    callOnHold = true;

    emit activeCallChanged(activeEtbu,"HOLD");
}

void Master_communication::resumeCall()
{
    //Added by pooja on 30 july 2026 for RU bydefalu slave unit
    if(!m_isMaster)
    {
        qDebug()<<"Slave cannot Resume ETBU call.";
        return;
    }
    //Added done by pooja on 30 july 2026 for RU bydefalu slave unit

    if (!callActive)
        return;

    if (!callOnHold)
        return;

    QByteArray frame = master_frames::createTalkResponse(responderAddress,activeEtbu);

    serial->write(frame);
    //added by pooja on 16 jul
    serial->flush();
    serial->waitForBytesWritten(100);
    //added done by pooja

    qDebug() << "Resume Response:" <<  frameToString(frame);

    EtbuInfo &etbu = etbuArray[activeEtbu];

    etbu.state = EtbuState::Talk;

    callOnHold = false;

    emit activeCallChanged(activeEtbu,"TALK");
}

QString Master_communication::frameToString(const QByteArray &frame)
{
    QString result;

    for(unsigned char byte : frame)
    {
        result += QString("%1 ").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
    }

    return result;
}

void Master_communication::startCommunication()
{
    if(serial->open(QIODevice::ReadWrite))
    {
        qDebug() << "Serial Port Opened";
    }
    else
    {
        qDebug() << "Failed to Open Port:"
                 << serial->errorString();
        return;
    }

    //pollTimer->start(50);  //commented by pooja to RU by default slave unit
    qDebug() << "***** NEW BUILD 1 AUG *****";
    //pollTimer->start(1000);//added by pooja on 16 jul 2026
    qDebug() << "Polling Started";

    //commented by pooja to RU by default slave unit
    //if (m_isMaster)
    //    peerPollTimer->start(1000);   // clause 5.3.2 - Master health-polls Slave every second
    //commented done by pooja to RU by default slave unit

    //Added by pooja to RU by default slave unit
    if(m_isMaster)
    {
        pollTimer->start(50);
        peerPollTimer->start(1000);

        qDebug() << "Response Unit started as MASTER";
    }
    else
    {
        qDebug() << "Response Unit started as SLAVE";
    }
    //Added done by pooja to RU by default slave unit

}


void Master_communication::stopCommunication()
{
    pollTimer->stop();
    peerPollTimer->stop();

    if (serial->isOpen())
        serial->close();

}

bool Master_communication::isRunning() const
{
    return serial->isOpen();
}

bool Master_communication::isCallOnHold() const
{
    return callOnHold;
}

bool Master_communication::isCallActive() const
{
    return callActive;
}

QByteArray Master_communication::createFrameForState(quint8 address)
{
    switch(etbuArray[address].state)
    {
    case EtbuState::Normal:

        return master_frames::createPollFrame(responderAddress,address);

    case EtbuState::Queued:

        return master_frames::createQueueResponse(responderAddress,address);

    case EtbuState::Talk:

        return master_frames::createTalkResponse(responderAddress,address);

    case EtbuState::Hold:

        return master_frames::createHoldResponse(responderAddress,address);

    default:

        return master_frames::createPollFrame(responderAddress,address);
    }
}

void Master_communication::onTimeout()
{
    qDebug()<< "ETBU 10 TIMEOUT";

    qDebug()<< "NO RESPONSE RECEIVED";

    timeoutCount++;
}

void Master_communication::checkEtbuoffline()
{
    for(int i = 1; i <= 64; i++)
    {
        EtbuInfo &etbu = etbuArray[i];

        qint64 seconds;

        if(etbu.lastResponse.isValid())
        {
            seconds =
                etbu.lastResponse.secsTo(
                    QDateTime::currentDateTime());
        }
        else
        {
            seconds =
                systemStartTime.secsTo(
                    QDateTime::currentDateTime());
        }

        if(seconds > 20)
        {
            if(etbu.online)
            {
                etbu.online = false;
                qDebug()<< "ETBU" << etbu.address << "OFFLINE";

                emit linkFault(QString("ETBU %1 went OFFLINE").arg(etbu.address));
                emit etbuOffline(etbu.address);
            }
        }
    }
}

void Master_communication::checkCallEscalation()
{
    // Clause 5.2/7.1: "If the guard does not pick up the request of ETB
    // within 30 sec (programmable), the call buzzer... shall be transferred
    // to driver side ETB response unit." Both Response Units independently
    // see every ETBU frame on the shared bus (the Slave "listens to all the
    // reply of ETBU units" per clause 5's preamble), so each unit can detect
    // an unanswered call on its own without needing a dedicated hand-off
    // frame - this is what makes the "transfer to driver side" behavior work
    // without inventing a function code the spec doesn't define.

    QDateTime now = QDateTime::currentDateTime();

    for (auto it = m_callQueuedSince.constBegin(); it != m_callQueuedSince.constEnd(); ++it)
    {
        quint8 address = it.key();

        if (!callQueue.contains(address))
            continue;   // already answered/ended, tracking will be cleared elsewhere

        if (m_escalatedCalls.contains(address))
            continue;   // already escalated once, don't repeat

        int secsWaiting = it.value().secsTo(now);

        if (secsWaiting >= m_escalationThresholdSecs)
        {
            m_escalatedCalls.insert(address);
            qDebug() << "Call from ETBU" << address << "unanswered for" << secsWaiting
                      << "s - escalating (clause 5.2)";
            emit linkFault(QString("Call from ETBU %1 unanswered for %2s - escalated")
                               .arg(address).arg(secsWaiting));
            //emit callEscalated(address);//commented by pooja on 1 august 2026
            emit callEscalated(address);//Added by pooja on 3 august 2026

            //Commented by pooja on 3 august 2026
            // //Added by pooja on 1 august 2026
            // if(secsWaiting >= 30)
            // {
            //     qDebug() << "***Escalating call to Driver Response Unit****";

            //     requestBecomeMaster();   // or a new function like transferMastership()
            // }
            // //Added done by pooja on 1 august 2026
            //Commented done by pooja on 3 august 2026


        }
    }
}

const EtbuInfo&
Master_communication::getEtbuInfo(int address) const
{
    return etbuArray[address];
}

// ============================================================
// Section 5 - Master Response unit <-> Slave Response unit
// (ICF/ED/P/001 clause 5, function codes 0x8D/0x8E/0x8F)
// ============================================================

void Master_communication::pollPeerResponseUnit()
{
    // Only the Master initiates this poll (clause 5.3.2). If we are Slave,
    // this timer is stopped in startCommunication()/handleResponseUnitFrame()
    // so this function won't fire.
    if (!m_isMaster)
        return;

    //QByteArray frame = master_frames::createResponseUnitActive(MASTER_RU_ADDRESS, SLAVE_RU_ADDRESS);  //commented by pooja on 31 july 2026
    QByteArray frame = master_frames::createResponseUnitActive(responderAddress,SLAVE_RU_ADDRESS);      //Added by pooja on 31 july 2026 for use the current address
    serial->write(frame);
    serial->flush();
    serial->waitForBytesWritten(100);

    qDebug() << "Peer RU poll (0x8E):" << frameToString(frame);

    // If the Slave hasn't answered in a while, flag it - mirrors the
    // ETBU 10-20s silence rule in clause 4.3's Table-1 note.
    if (m_peerLastResponse.isValid() &&
        m_peerLastResponse.secsTo(QDateTime::currentDateTime()) > 20)
    {
        if (m_peerOnline)
        {
            m_peerOnline = false;
            emit linkFault("Slave Response Unit went OFFLINE");
            emit peerStatusChanged(false);
        }
    }
}

void Master_communication::requestBecomeMaster()
{
    if (m_isMaster)
    {
        qDebug() << "requestBecomeMaster() ignored - this unit is already Master";
        return;
    }

    //Added by pooja on 1 august 2026
    if (m_masterRequestInFlight)
    {
        qDebug() << "requestBecomeMaster() ignored - a request is already pending, waiting for 0x8F reply";
        return;
    }

    m_masterRequestInFlight = true;   // block any further 0x8D until we get a reply or time out
    //Added done by pooja on 1 august 2026


    //QByteArray frame = master_frames::createResponseUnitActiveRequest(SLAVE_RU_ADDRESS, MASTER_RU_ADDRESS);   //commented by pooja on 31 july 2026
    QByteArray frame =master_frames::createResponseUnitActiveRequest(responderAddress,MASTER_RU_ADDRESS);       //Added by pooja on 31 july 2026 for use the current address
    serial->write(frame);
    serial->flush();
    serial->waitForBytesWritten(100);

    qDebug() << "Sent Active Request (0x8D) - asking to become Master:" << frameToString(frame);

    // We wait for the current Master to answer with 0x8F (clause 5.3.1)
    // before actually promoting ourselves - see handleResponseUnitFrame().

    //Added by pooja on 1 august 2026
    // If no reply comes back within 2s (peer offline / not actually Master
    // yet), onMasterRequestTimeout() clears m_masterRequestInFlight so the
    // operator can try again.
    masterRequestTimer->start(2000);
}

void Master_communication::onMasterRequestTimeout()
{
    if (m_isMaster)
        return;   // we were promoted in the meantime - nothing to clean up

    qDebug() << "requestBecomeMaster() timed out - no 0x8F reply received";

    m_masterRequestInFlight = false;
    emit linkFault("Master promotion request timed out - no reply from peer Response Unit");
    emit masterRequestFailed();   // UI re-enables the 'Slave Unit' button

    //Added done by pooja on 1 august 2026
}

//Added by pooja on 3 august 2026
void Master_communication::onMasterSilenceTimeout()
{
    // Runs only if masterWatchdogTimer actually fires - which only happens
    // if we were Slave, had seen a genuine Master (m_masterEverSeen), and
    // then received no further 0x8E poll for MASTER_SILENCE_TIMEOUT_MS.
    if (m_isMaster)
        return;   // became Master through some other path already - nothing to do

    qDebug() << "*******************************************";
    qDebug() << "MASTER RESPONSE UNIT SILENT FOR"
             << (MASTER_SILENCE_TIMEOUT_MS / 1000) << "SECONDS";
    qDebug() << "Auto-promoting this unit to MASTER (failover)";
    qDebug() << "*******************************************";

    m_isMaster = true;
    responderAddress = MASTER_RU_ADDRESS;

    m_masterRequestInFlight = false;   // any stale pending 0x8D request is moot now
    masterRequestTimer->stop();

    m_masterEverSeen = false;          // re-arm only if/when we're demoted back to Slave later
    masterWatchdogTimer->stop();

    m_peerOnline = false;              // the old Master is presumed down until it proves otherwise
    m_peerLastResponse = QDateTime();

    pollTimer->start(50);
    peerPollTimer->start(1000);

    emit roleChanged(true);
    emit linkFault("Master Response Unit not responding for 30s - this unit auto-promoted to MASTER (failover)");

}


// //void Master_communication::handleResponseUnitFrame(quint8 srcAddress, quint8 functionCode)
// void Master_communication::handleResponseUnitFrame(
//     quint8 srcAddress,
//     quint8 destAddress,
//     quint8 functionCode)
// {
//     switch (functionCode)
//     {
//     case 0x8D:  // Active Request - a Slave wants to become Master (clause 5.3.1)
//     {
//         if (!m_isMaster)
//         {
//             // We're the Slave and this is (unexpectedly) an Active Request
//             // addressed to us - nothing to do, only a Master demotes on 0x8D.
//             break;
//         }

//         qDebug() << "Received Active Request (0x8D) from" << srcAddress
//                   << "- demoting self to Slave";

//         // Confirm the demotion by sending Inactive (0x8F) back to the
//         // requester, which promotes IT to Master.
//         QByteArray frame = master_frames::createResponseUnitInactive(MASTER_RU_ADDRESS, srcAddress);
//         serial->write(frame);
//         serial->flush();
//         serial->waitForBytesWritten(100);

//         qDebug() << "Sent Inactive (0x8F) confirming demotion:" << frameToString(frame);

//         m_isMaster = false;
//         pollTimer->stop();       // Slave does not poll ETBUs
//         peerPollTimer->stop();   // Slave does not poll the peer either

//         emit roleChanged(false);
//         qDebug()<<"***** THIS UNIT IS NOW SLAVE *****"; //Added by pooja on 30 july 2026
//         emit linkFault("This unit demoted to Slave Response Unit");
//         break;
//     }

//     case 0x8E:  // Active - the Master's periodic health poll (clause 5.3.2)
//     {
//         if (m_isMaster)
//         {
//             // Two Masters on the bus should not happen; log it, don't act.
//             qDebug() << "WARNING: received Active(0x8E) poll while we are Master - ignoring";
//             break;
//         }

//         QByteArray frame = master_frames::createResponseUnitInactive(SLAVE_RU_ADDRESS, srcAddress);
//         serial->write(frame);
//         serial->flush();
//         serial->waitForBytesWritten(100);

//         qDebug() << "Replied Inactive (0x8F) to Master's health poll:" << frameToString(frame);
//         break;
//     }

//     case 0x8F:  // Inactive
//     {
//         if (m_isMaster)
//         {
//             // This is the Slave's normal health-poll acknowledgement.
//             bool wasOffline = !m_peerOnline;
//             m_peerOnline = true;
//             m_peerLastResponse = QDateTime::currentDateTime();

//             if (wasOffline)
//                 emit peerStatusChanged(true);
//         }
//         else
//         {
//             // We were the Slave and requested promotion (0x8D) - this 0x8F
//             // is the current Master confirming it has demoted itself.
//             qDebug() << "Received Inactive (0x8F) confirmation - promoting self to Master";

//             m_isMaster = true;
//             m_peerOnline = true;
//             m_peerLastResponse = QDateTime::currentDateTime();

//             pollTimer->start(50);
//             peerPollTimer->start(1000);

//             emit roleChanged(true);
//             emit linkFault("This unit promoted to Master Response Unit");
//         }
//         break;
//     }

//     default:
//         qDebug() << "Unhandled response-unit function code:" << functionCode;
//         break;
//     }
// }


void Master_communication::handleResponseUnitFrame(
    quint8 srcAddress,
    quint8 destAddress,
    quint8 functionCode)
{
    // Determine this Response Unit's address
    quint8 myAddress = m_isMaster ? MASTER_RU_ADDRESS : SLAVE_RU_ADDRESS;

    qDebug() << "================================";
    qDebug() << "Response Unit Frame Received";
    qDebug() << "Source Address      :" << srcAddress;
    qDebug() << "Destination Address :" << destAddress;
    qDebug() << "Function Code       :" << QString("0x%1")
                                               .arg(functionCode,2,16,QLatin1Char('0')).toUpper();
    qDebug() << "My Address          :" << myAddress;
    qDebug() << "Current Role        :" << (m_isMaster ? "MASTER" : "SLAVE");
    qDebug() << "================================";

    // Ignore frames not addressed to this Response Unit
    if(destAddress != myAddress)
    {
        qDebug() << "Frame not addressed to this unit. Ignoring...";
        return;
    }

    switch(functionCode)
    {
    //---------------------------------------------------------
    // Active Request (0x8D)
    //---------------------------------------------------------
    case 0x8D:
    {
        if(!m_isMaster)
        {
            qDebug() << "Ignoring Active Request because this unit is already SLAVE.";
            return;
        }

        qDebug() << "Received Active Request from Slave.";
        qDebug() << "Changing this unit to SLAVE.";

        QByteArray frame =
            master_frames::createResponseUnitInactive(
                MASTER_RU_ADDRESS,
                srcAddress);

        serial->write(frame);
        serial->flush();
        serial->waitForBytesWritten(100);

        qDebug() << "TX :" << frameToString(frame);

        m_isMaster = false;

        m_masterRequestInFlight = false;   // Added by pooja on 1 august 2026 fresh Slave state - clear any stale guard
        //Added by pooja on 3 august 2026 for master timeout
        m_masterEverSeen = false;          // will re-arm on the new Master's first 0x8E poll
        masterWatchdogTimer->stop();
        //Added done by pooja on 3 august 2026 for master timeout


        responderAddress = SLAVE_RU_ADDRESS;

        //Added by pooja on 31 july 2026 to check When becoming Master, update both the role and the address.
        qDebug() << "*****************************";
        qDebug() << "THIS UNIT IS NOW SLAVE";
        qDebug() << "My Address =" << responderAddress;
        qDebug() << "*****************************";
        //Added done by pooja on 31 july 2026 to check When becoming Master, update both the role and the address.

        pollTimer->stop();
        peerPollTimer->stop();

        emit roleChanged(false);

        qDebug() << "*******************************";
        qDebug() << "***** THIS UNIT IS SLAVE ******";
        qDebug() << "*******************************";

        emit linkFault("This unit demoted to Slave Response Unit");

        break;
    }

        //---------------------------------------------------------
        // Active Poll (0x8E)
        //---------------------------------------------------------
    case 0x8E:
    {
        if(m_isMaster)
        {
            qDebug() << "Received Active Poll while already MASTER.";
            break;
        }

        qDebug() << "Received Active Poll from MASTER.";

        // Added by pooja on 3 August 2026 Master is alive and polling us - (re)arm the 30s failover watchdog.
        m_masterEverSeen = true;
        masterWatchdogTimer->start(MASTER_SILENCE_TIMEOUT_MS);
        // Added done by pooja on 3 August 2026 Master is alive and polling us - (re)arm the 30s failover watchdog.



        QByteArray frame =
            master_frames::createResponseUnitInactive(
                SLAVE_RU_ADDRESS,
                srcAddress);

        serial->write(frame);
        serial->flush();
        serial->waitForBytesWritten(100);

        qDebug() << "TX :" << frameToString(frame);

        break;
    }

        //---------------------------------------------------------
        // Inactive (0x8F)
        //---------------------------------------------------------
    case 0x8F:
    {
        if(m_isMaster)
        {
            qDebug() << "Received Inactive acknowledgement from Slave.";

            bool wasOffline = !m_peerOnline;

            m_peerOnline = true;
            m_peerLastResponse = QDateTime::currentDateTime();

            if(wasOffline)
            {
                emit peerStatusChanged(true);
            }
        }
        else
        {
            qDebug() << "Received Inactive confirmation.";
            qDebug() << "Changing this unit to MASTER.";

            masterRequestTimer->stop();         //Added by pooja on 1 august 2026
            m_masterRequestInFlight = false;   //Added by pooja on 1 august 2026 clear the guard - the request that we were waiting on just succeeded

            masterWatchdogTimer->stop();       //Added by pooja on 3 august 2026 we ARE the Master now - stop watching for one
            m_masterEverSeen = false;           //Added by pooja on 3 august 2026 re-arm only if/when we're demoted back to Slave


            m_isMaster = true;

            //Added by pooja on 31 july 2026 to check When becoming Master, update both the role and the address.
            responderAddress = MASTER_RU_ADDRESS;

            qDebug() << "*****************************";
            qDebug() << "THIS UNIT IS NOW MASTER";
            qDebug() << "My Address =" << responderAddress;
            qDebug() << "*****************************";
            //Added done by pooja on 31 july 2026 to check When becoming Master, update both the role and the address.

            m_peerOnline = true;
            m_peerLastResponse = QDateTime::currentDateTime();

            pollTimer->start(50);
            peerPollTimer->start(1000);

            emit roleChanged(true);

            qDebug() << "*******************************";
            qDebug() << "***** THIS UNIT IS MASTER *****";
            qDebug() << "*******************************";

            emit linkFault("This unit promoted to Master Response Unit");
        }

        break;
    }

        //---------------------------------------------------------
    default:
    {
        qDebug() << "Unknown Response Unit Function Code:"
                 << functionCode;
        break;
    }
    }
}


