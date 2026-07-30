
 /******************************************************************************
 * Module      : DatabaseManager
 * Description : Handles SQLite database creation, call log storage,
 *               log retrieval and automatic deletion of records older
 *               than 30 days.
 *
 * Specification Mapping:
 * Clause 8.1 - Requirement: Software shall be able to playback recorded audio
 * stream with time,date and location stamping.
 * Implemented By: Priyanka
 ******************************************************************************/

#include "databasemanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QFile>
#include <QDateTime>

DatabaseManager::DatabaseManager()
{
}

bool DatabaseManager::initialize()
{
    QSqlDatabase db =
        QSqlDatabase::addDatabase("QSQLITE");

    db.setDatabaseName("etbu_logs.db");

    if (!db.open())
    {
        qDebug() << db.lastError();
        return false;
    }

    QSqlQuery query;

    bool result = query.exec(
        "CREATE TABLE IF NOT EXISTS call_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "etbu_address INTEGER,"
        "call_start_time TEXT,"
        "call_end_time TEXT,"
        "operator_id,"
        "gps_location TEXT,"
        "audio_file TEXT"
        ");");

    qDebug() << "Table creation:" << result;

    if (!result)
    {
        qDebug() << query.lastError();
    }

    return result;
}

bool DatabaseManager::insertLog(
    int etbuAddress,
    const QString &startTime,
    const QString &endTime,
     const QString &operatorId,
    const QString &gpsLocation,
    const QString &audioFile)
{
    QSqlQuery query;

    query.prepare(
        "INSERT INTO call_logs "
        "(etbu_address, call_start_time, call_end_time, operator_id, gps_location, audio_file) "
        "VALUES (?, ?, ?, ?, ?, ?)");

    query.addBindValue(etbuAddress);
    query.addBindValue(startTime);
    query.addBindValue(endTime);
    query.addBindValue(operatorId);
    query.addBindValue(gpsLocation);
    query.addBindValue(audioFile);

    bool ok = query.exec();

    qDebug() << "Insert log:" << ok;

    if (!ok)
    {
        qDebug() << query.lastError();
    }

    return ok;
}

void DatabaseManager::deleteOldLogs()
{
    QSqlQuery query;

    bool ok = query.exec(
        "SELECT id, audio_file, call_end_time "
        "FROM call_logs");

    if (!ok)
    {
        qDebug() << query.lastError();
        return;
    }

    while (query.next())
    {
        int id = query.value(0).toInt();

        QString audioFile =
            query.value(1).toString();

        QString endTimeString =
            query.value(2).toString();

        QDateTime endTime =
            QDateTime::fromString(
                endTimeString,
                Qt::ISODate);

        if (!endTime.isValid())
            continue;

        int daysOld =
            endTime.daysTo(
                QDateTime::currentDateTime());

        if (daysOld >= 30)   // Clause 6.3 requires minimum 30 days retention
        {
            qDebug() << "Deleting old log ID:" << id;

            if (QFile::exists(audioFile))
            {
                QFile::remove(audioFile);

                qDebug()
                    << "Deleted audio:"
                    << audioFile;
            }

            QSqlQuery deleteQuery;

            deleteQuery.prepare(
                "DELETE FROM call_logs "
                "WHERE id = ?");

            deleteQuery.addBindValue(id);

            deleteQuery.exec();
        }
    }
}
