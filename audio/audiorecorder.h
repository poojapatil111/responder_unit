#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H
#include <QByteArray>
#include <QString>
#include <cstdint>
#include <atomic>
#include <thread>
#include <alsa/asoundlib.h>


class AudioRecorder
{
public:
    AudioRecorder();
    ~AudioRecorder();

    bool startRecording(const QString &fileName);
    void stopRecording();
    bool play(const QString &fileName);

    QString generateFileName(uint8_t etbuNumber);
    QString generateFilePath(uint8_t etbuNumber);


private:
    void recordThreadFunction(QString fileName);

    std::thread m_thread;
    std::atomic<bool> m_isRecording{false};

    QString m_currentFilePath;
};

#endif // AUDIORECORDER_H
