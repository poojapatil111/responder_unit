#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>

class DatabaseManager
{
public:
    DatabaseManager();
    void deleteOldLogs();

    bool initialize();
    bool insertLog(
        int etbuAddress,
        const QString &startTime,
        const QString &endTime,
        const QString &operatorId,
        const QString &gpsLocation,
        const QString &audioFile);
};

#endif
