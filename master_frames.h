#ifndef MASTER_FRAMES_H
#define MASTER_FRAMES_H
#include <QByteArray>

class master_frames
{   
public:
    static QByteArray createPollFrame(
        quint8 responderAddress,
        quint8 etbuAddress);

    static QByteArray createQueueResponse(
        quint8 responderAddress,
        quint8 etbuAddress);

    static QByteArray createTalkResponse(
        quint8 responderAddress,
        quint8 etbuAddress);

    static QByteArray createHoldResponse(
        quint8 responderAddress,
        quint8 etbuAddress);

    static QByteArray createEndCallResponse(
        quint8 responderAddress,
        quint8 etbuAddress);

    // ---- Section 5: Master Response unit <-> Slave Response unit ----
    // Same 6-byte frame shape as the ETBU frames (start/src/dst/fc/crc16),
    // but function codes 0x8D/0x8E/0x8F are reserved for response-unit-to
    // -response-unit traffic and never collide with the 0x00-0x03 ETBU
    // function codes, so the two protocols can share the bus safely.

    // fc = 0x8D - "Response unit Active request": sent by a Slave that wants
    // to be promoted to Master.
    static QByteArray createResponseUnitActiveRequest(
        quint8 srcAddress,
        quint8 destAddress);

    // fc = 0x8E - "Response unit Active": sent by the Master as a periodic
    // health poll to the Slave (clause 5.3.2).
    static QByteArray createResponseUnitActive(
        quint8 srcAddress,
        quint8 destAddress);

    // fc = 0x8F - "Response Unit Inactive": sent by the Slave to acknowledge
    // a poll, OR sent by the Master to confirm it has demoted itself after
    // receiving an Active Request (clause 5.3.1).
    static QByteArray createResponseUnitInactive(
        quint8 srcAddress,
        quint8 destAddress);

    static quint16 calculateCRC(const QByteArray &data);

private:

};

#endif // MASTER_FRAMES_H
