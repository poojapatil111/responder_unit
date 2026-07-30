#include "master_frames.h"
#include "master_communication.h"


QByteArray master_frames::createPollFrame(
    quint8 responderAddress,
    quint8 etbuAddress)
{
    QByteArray frame;

    frame.append(char(0xFF));           // start byte
    frame.append(char(responderAddress));            // SRC ADD

    frame.append(char(etbuAddress));     // ETBU detination address

    frame.append(char(0x00));             // poll Function code

    quint16 crc = calculateCRC(frame);

    // Master Sends the MSB  first
    frame.append((char)((crc >> 8) & 0xFF));  // CRC MSB
    frame.append((char)(crc & 0xFF));         // CRC LSB


    return frame;
}

QByteArray master_frames:: createQueueResponse(
    quint8 responderAddress,
    quint8 etbuAddress)
{
    QByteArray frame;

    frame.append(char(0xFF));
    frame.append(char(responderAddress));
    frame.append(char(etbuAddress));
    frame.append(char(0x01));

    quint16 crc = calculateCRC(frame);

    frame.append(char((crc >> 8) & 0xFF));
    frame.append(char(crc & 0xFF));

    return frame;
}

QByteArray master_frames::createTalkResponse(
    quint8 responderAddress,
    quint8 etbuAddress)                                       //fc = 0x02
{
    QByteArray frame;

    frame.append(char(0xFF));
    frame.append(char(responderAddress));
    frame.append(char(etbuAddress));
    frame.append(char(0x02));

    quint16 crc = calculateCRC(frame);

    frame.append(char((crc >> 8) & 0xFF));
    frame.append(char(crc & 0xFF));

    return frame;
}

QByteArray master_frames::createHoldResponse(
    quint8 responderAddress,
    quint8 etbuAddress)                              //fc = 0x03
{
    QByteArray frame;

    frame.append(char(0xFF));
    frame.append(char(responderAddress));
    frame.append(char(etbuAddress));
    frame.append(char(0x03));

    quint16 crc = calculateCRC(frame);

    frame.append(char((crc >> 8) & 0xFF));
    frame.append(char(crc & 0xFF));

    return frame;
}

QByteArray master_frames::createEndCallResponse(
    quint8 responderAddress,
    quint8 etbuAddress)                                 //fc = 0x00
{
    QByteArray frame;

    frame.append(char(0xFF));
    frame.append(char(responderAddress));
    frame.append(char(etbuAddress));
    frame.append(char(0x00));

    quint16 crc = calculateCRC(frame);

    frame.append(char((crc >> 8) & 0xFF));
    frame.append(char(crc & 0xFF));

    return frame;
}

// ---- Section 5: Master Response unit <-> Slave Response unit ----

QByteArray master_frames::createResponseUnitActiveRequest(
    quint8 srcAddress,
    quint8 destAddress)                                  //fc = 0x8D
{
    QByteArray frame;

    frame.append(char(0xFF));
    frame.append(char(srcAddress));
    frame.append(char(destAddress));
    frame.append(char(0x8D));

    quint16 crc = calculateCRC(frame);

    frame.append(char((crc >> 8) & 0xFF));
    frame.append(char(crc & 0xFF));

    return frame;
}

QByteArray master_frames::createResponseUnitActive(
    quint8 srcAddress,
    quint8 destAddress)                                  //fc = 0x8E
{
    QByteArray frame;

    frame.append(char(0xFF));
    frame.append(char(srcAddress));
    frame.append(char(destAddress));
    frame.append(char(0x8E));

    quint16 crc = calculateCRC(frame);

    frame.append(char((crc >> 8) & 0xFF));
    frame.append(char(crc & 0xFF));

    return frame;
}

QByteArray master_frames::createResponseUnitInactive(
    quint8 srcAddress,
    quint8 destAddress)                                  //fc = 0x8F
{
    QByteArray frame;

    frame.append(char(0xFF));
    frame.append(char(srcAddress));
    frame.append(char(destAddress));
    frame.append(char(0x8F));

    quint16 crc = calculateCRC(frame);

    frame.append(char((crc >> 8) & 0xFF));
    frame.append(char(crc & 0xFF));

    return frame;
}

quint16 master_frames::calculateCRC(const QByteArray &data)
{
    quint16 crc = 0xFFFF;

    for (unsigned char byte : data)
    {
        crc ^= byte;

        for (int i = 0; i < 8; i++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}
