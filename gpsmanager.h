#ifndef GPSMANAGER_H
#define GPSMANAGER_H

#include <QObject>
#include <QSerialPort>
#include <QString>

class GpsManager : public QObject
{
    Q_OBJECT

public:
    explicit GpsManager(QObject *parent = nullptr);

    // Call this once at startup to open the GPS serial port
    bool start(const QString &portName = "/dev/ttyGPS0", qint32 baud = 9600);

    void stop();

    // Returns "lat,lon" as text, or "--" if we don't have a fix yet
    QString currentLocationString() const;

    bool hasFix() const { return m_hasFix; }

signals:
    void fixAcquired();   // GPS found satellites - yay!
    void fixLost();       // GPS lost satellites - uh oh
    void locationChanged(const QString &location);    // Added by POOJA on 21 july 2026

private slots:
    void onReadyRead();

private:
    void parseLine(const QString &line);
    void parseGPRMC(const QStringList &fields);
    void parseGPGGA(const QStringList &fields);

    QSerialPort *m_serial;
    QByteArray   m_buffer;

    double m_lat = 0.0;
    double m_lon = 0.0;
    bool   m_hasFix = false;
};

#endif // GPSMANAGER_H
