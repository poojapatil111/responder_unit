/******************************************************************************
 * Module      : AudioRecorder
 * Description : Records ETBU audio streams and stores WAV files
 *               for offline playback.
 *
 * Specification Mapping:
 * Clause 8.1 - Audio recording and playback
 * Implemented By: Priyanka
 ******************************************************************************/
#include "audiorecorder.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QMetaObject>
#include <QProcess>

#pragma pack(push, 1)
struct WavHeader
{
    char riff[4] = {'R','I','F','F'};
    quint32 chunkSize;
    char wave[4] = {'W','A','V','E'};
    char fmt[4] = {'f','m','t',' '};
    quint32 subchunk1Size = 16;
    quint16 audioFormat = 1;
    quint16 numChannels = 1;
    quint32 sampleRate = 16000;
    quint32 byteRate = 16000 * 1 * 16 / 8;
    quint16 blockAlign = 2;
    quint16 bitsPerSample = 16;
    char data[4] = {'d','a','t','a'};
    quint32 dataSize;
};
#pragma pack(pop)

AudioRecorder::AudioRecorder()
{

}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
}

/* Creates a dedicated recording thread that captures
* audio from the STM32MP1 audio input device and
* stores the data in WAV format.
*/

bool AudioRecorder::startRecording(const QString &fileName)
{
    if (m_isRecording)
    {
        qDebug() << "Already recording";
        return false;
    }

    m_isRecording = true;

    m_thread = std::thread(
        &AudioRecorder::recordThreadFunction,
        this,
        fileName);

    return true;
}

/* Signals the recording thread to exit and waits
 * for the thread to terminate safely.
 */

void AudioRecorder::stopRecording()
{
    if (!m_isRecording)
        return;

    qDebug() << "STOP REQUEST RECEIVED";

    m_isRecording = false;

    if (m_thread.joinable())
        m_thread.join();
}

/* Configures the ALSA capture device, records PCM
 * audio samples, writes them into a WAV file and
 * updates the WAV header after recording completes.
 *
 */
void AudioRecorder::recordThreadFunction(QString fileName)
{
    qDebug() << "Recording thread started";
    qDebug() << "File requested:" << fileName;

    /* Configure ALSA capture device for 16 kHz mono PCM recording */
    snd_pcm_t *handle = nullptr;
    snd_pcm_hw_params_t *params;

    int err = snd_pcm_open(&handle, "hw:0,1", SND_PCM_STREAM_CAPTURE, 0);

    qDebug() << "[ALSA] snd_pcm_open returned:" << err;

    if (err < 0)
    {
        qDebug() << "[ALSA ERROR] Cannot open device:";
        qDebug() << snd_strerror(err);

        m_isRecording = false;
        return;
    }

    qDebug() << "[ALSA] Device opened successfully";

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);

    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

    unsigned int rate = 16000;
    snd_pcm_hw_params_set_rate_near(handle, params, &rate, nullptr);

    snd_pcm_hw_params_set_channels(handle, params, 1);

    snd_pcm_hw_params(handle, params);

    /* Create WAV file and reserve space for the header */
    QFile file(fileName);
    qDebug() << "[FILE] Trying to open:" << fileName;

    if (!file.exists())
    {
        qDebug() << "[FILE] File does not exist yet (will be created)";
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qDebug() << "[FILE ERROR] Cannot open file for writing";
        qDebug() << file.errorString();
        snd_pcm_close(handle);
        m_isRecording = false;
        return;
    }

    qDebug() << "[FILE] Opened successfully";
    WavHeader header{};
    file.write(reinterpret_cast<char*>(&header), sizeof(WavHeader));

    qint16 buffer[1024];
    qint64 totalBytes = 0;

    qDebug() << "[LOOP] Starting capture loop...";
    /* Continuously capture audio frames until stop request is received */
    while (m_isRecording)
    {
        int rc = snd_pcm_readi(handle, buffer, 1024);

        if (rc < 0)
        {
            qDebug() << "[ALSA] Read error:";
            qDebug() << snd_strerror(rc);

            snd_pcm_prepare(handle);
            continue;
        }

        file.write(reinterpret_cast<char*>(buffer), rc * sizeof(qint16));
        totalBytes += rc * sizeof(qint16);
    }
    qDebug() << "[LOOP] Exiting capture loop...";

    header.dataSize = totalBytes;
    header.chunkSize = 36 + totalBytes;

    file.seek(0);
    file.write(reinterpret_cast<char*>(&header), sizeof(WavHeader));

    file.close();
    snd_pcm_close(handle);

    qDebug() << "Recording stopped";
}

/* Uses the ALSA aplay utility to playback stored
 ETBU communication recordings.*/
bool AudioRecorder::play(const QString &fileName)
{
    if (!QFile::exists(fileName))
    {
        qDebug() << "File not found";
        return false;
    }

    return QProcess::startDetached(
        "aplay",
        QStringList() << "-D" << "plughw:0,0" << fileName);
}

/*Generates a unique recording file name.
* Format: ETBU_<ETBU Number>_<Timestamp>.wav*/
QString AudioRecorder::generateFileName(uint8_t etbuNumber)
{
    QString timestamp =
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");

    return QString("ETBU_%1_%2.wav").arg(etbuNumber).arg(timestamp);
}

/* Recordings are organized by date:
 * audio/YYYY/MM/DD/*/
QString AudioRecorder::generateFilePath(uint8_t etbuNumber)
{
    QDate date = QDate::currentDate();

    QString folder =
        QString("audio/%1/%2/%3")
            .arg(date.toString("yyyy"))
            .arg(date.toString("MM"))
            .arg(date.toString("dd"));

    QDir dir;
    dir.mkpath(folder);

    return folder + "/" + generateFileName(etbuNumber);
}

