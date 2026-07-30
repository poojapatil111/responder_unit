/******************************************************************************
 * Module      : GpsManager
 * Description : Reads NMEA sentences from a u-blox NEO GPS module over UART
 *               and provides the current latitude/longitude as text so it
 *               can be stamped on every Emergency Talk Back call log.
 *
 * Specification Mapping:
 * Clause 7.1 - Call generated alarming shall be Time, Date & Location stamped
 * Clause 8.1 - Software shall support location stamping of recorded audio
 ******************************************************************************/

#include "gpsmanager.h"
#include <QDebug>
#include <cmath>

GpsManager::GpsManager(QObject *parent) : QObject(parent)
{
    m_serial = new QSerialPort(this);
}

bool GpsManager::start(const QString &portName, qint32 baud)
{
    m_serial->setPortName(portName);
    m_serial->setBaudRate(baud);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadOnly))
    {
        qDebug() << "GPS: Failed to open port"
                 << portName
                 << m_serial->errorString();
        return false;
    }

    connect(m_serial,
            &QSerialPort::readyRead,
            this,
            &GpsManager::onReadyRead);

    qDebug() << "GPS: Port opened on"
             << portName
             << "at"
             << baud
             << "baud";

    return true;
}

void GpsManager::stop()
{
    if (m_serial->isOpen())
        m_serial->close();
}

void GpsManager::onReadyRead()
{
    m_buffer.append(m_serial->readAll());

    // NMEA sentences are separated by newlines
    while (m_buffer.contains('\n'))
    {
        int idx = m_buffer.indexOf('\n');
        QString line = QString::fromLatin1(m_buffer.left(idx)).trimmed();
        m_buffer.remove(0, idx + 1);

        if (!line.isEmpty())
            parseLine(line);
    }
}

void GpsManager::parseLine(const QString &line)
{
    // Different modules/chips may prefix with GP or GN
    if (line.startsWith("$GPRMC") || line.startsWith("$GNRMC"))
    {
        parseGPRMC(line.split(','));
    }
    else if (line.startsWith("$GPGGA") || line.startsWith("$GNGGA"))
    {
        parseGPGGA(line.split(','));
    }
}

// $GPRMC,time,status(A/V),lat,N/S,lon,E/W,speed,course,date,...
void GpsManager::parseGPRMC(const QStringList &f)
{
    if (f.size() < 7)
        return;

    bool valid = (f[2] == "A");   // A = data valid, V = void (no fix)

    if (!valid)
    {
        if (m_hasFix)
        {
            m_hasFix = false;
            qDebug() << "GPS: Fix lost";
            emit fixLost();
        }
        return;
    }

    if (f[3].isEmpty() || f[5].isEmpty())
        return;

    // Convert NMEA ddmm.mmmm format to decimal degrees
    double rawLat = f[3].toDouble();
    double lat = std::floor(rawLat / 100.0) + std::fmod(rawLat, 100.0) / 60.0;
    if (f[4] == "S") lat = -lat;

    double rawLon = f[5].toDouble();
    double lon = std::floor(rawLon / 100.0) + std::fmod(rawLon, 100.0) / 60.0;
    if (f[6] == "W") lon = -lon;

    m_lat = lat;
    m_lon = lon;

    // ===== ADD BY POOJA THESE LINES HERE =====
    qDebug() << "Latitude :" << m_lat;
    qDebug() << "Longitude:" << m_lon;
    qDebug() << "Current Location:" << currentLocationString();
    // ===== ADD DONE BY POOJA THESE LINES HERE =====

    emit locationChanged(currentLocationString());     //ADD BY POOJA THESE LINE HERE


    if (!m_hasFix)
    {
        m_hasFix = true;
        qDebug() << "GPS: Fix acquired at" << m_lat << m_lon;
        emit fixAcquired();
    }
}

// $GPGGA,time,lat,N/S,lon,E/W,fixQuality,numSatellites,...
// Used as a backup source / cross-check of fix quality
void GpsManager::parseGPGGA(const QStringList &f)
{
    if (f.size() < 7)
        return;

    int fixQuality = f[6].toInt();  // 0 = no fix, 1 = GPS fix, 2 = DGPS fix

    if (fixQuality == 0)
    {
        if (m_hasFix)
        {
            m_hasFix = false;
            qDebug() << "GPS: Fix lost (GGA)";
            emit fixLost();
        }
    }
}

QString GpsManager::currentLocationString() const
{
    if (!m_hasFix)
        return "--";

    return QString("%1,%2")
        .arg(m_lat, 0, 'f', 6)
        .arg(m_lon, 0, 'f', 6);
}
