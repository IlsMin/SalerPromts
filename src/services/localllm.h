#ifndef LOCALLLM_H
#define LOCALLLM_H

#include "core/types.h"

#include <QObject>
#include <QString>
#include <QVector>

class QProcess;
class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

class LlamaServer : public QObject
{
    Q_OBJECT

public:
    explicit LlamaServer(const QString &roleLabel, QObject *parent = nullptr);
    ~LlamaServer() override;

    QString roleLabel() const { return m_roleLabel; }
    QString modelFileName() const { return m_modelFileName; }
    QString modelPath() const { return m_modelPath; }
    int port() const { return m_port; }
    int gpuLayers() const { return m_gpuLayers; }
    bool isReady() const { return m_ready; }
    bool isStarting() const { return m_starting; }
    QString lastError() const { return m_error; }

    bool matches(const QString &modelPath, int port, int gpuLayers, int context, int threads) const;
    bool sameConfig(const QString &modelPath, int port, int gpuLayers, int context, int threads) const;

    void startAsync(const QString &binDir,
                    const QString &modelPath,
                    int port,
                    int gpuLayers,
                    int context,
                    int threads);
    void stop();

    void generate(const QString &systemPrompt,
                  const QVector<ChatMessage> &history,
                  int maxTokens,
                  double temperature,
                  const QString &grammar = QString());
    void abortGeneration();

signals:
    void readyChanged(bool ready);
    void startFailed(const QString &modelFileName, const QString &error);
    void generationFinished(const QString &text);
    void generationFailed(const QString &error);
    void logLine(const QString &line);

private:
    void onProcessFinished(int exitCode);
    void pollHealth();
    void failStart(const QString &msg);
    void finishGeneration(const QString &text);
    void failGeneration(const QString &msg);
    void postChat(const QByteArray &body);
    void postCompletion(const QByteArray &body);
    QString extractContent(const QByteArray &body, QString *errorOut) const;
    void appendLog(const QByteArray &chunk);

    QString m_roleLabel;
    QProcess *m_proc = nullptr;
    QTimer *m_healthTimer = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_healthReply = nullptr;
    QNetworkReply *m_genReply = nullptr;

    QString m_binDir;
    QString m_modelPath;
    QString m_modelFileName;
    int m_port = 0;
    int m_gpuLayers = -1;
    int m_context = 0;
    int m_threads = 4;
    bool m_ready = false;
    bool m_starting = false;
    qint64 m_startDeadline = 0;
    QString m_error;
    QByteArray m_logTail;
    bool m_triedCompletionFallback = false;
    bool m_triedWithoutGrammar = false;
    QByteArray m_lastPayload;
    QString m_grammar;
};

class LocalLlm : public QObject
{
    Q_OBJECT

public:
    explicit LocalLlm(QObject *parent = nullptr);
    ~LocalLlm() override;

    LlamaServer *dialogServer() const { return m_dialog; }
    LlamaServer *analyzerServer() const { return m_analyzer; }

    bool usesSharedServer() const;
    bool isDialogReady() const;
    bool isAnalyzerReady() const;
    bool bothReady() const;
    QString dialogModelName() const;
    QString analyzerModelName() const;
    QString dialogDeviceLabel() const;
    QString analyzerDeviceLabel() const;

    void preloadFromSettings();
    void ensureDialog();
    void ensureAnalyzer();
    void shutdown();

    void generateDialog(const QString &systemPrompt,
                        const QVector<ChatMessage> &history,
                        int maxTokens = -1);
    void generateAnalyzer(const QString &systemPrompt,
                          const QVector<ChatMessage> &history,
                          int maxTokens = -1);
    void abortPendingGeneration();

    QStringList vramWarnings() const;

signals:
    void statusChanged(const QString &text);
    void serversReady();
    void analyzerServerReady();
    void serverFailed(const QString &message);
    void dialogReply(const QString &text);
    void analyzerReply(const QString &text);
    void dialogFailed(const QString &message);
    void analyzerFailed(const QString &message);

private:
    void onDialogReadyChanged(bool ready);
    void onAnalyzerReadyChanged(bool ready);
    void onInstanceFailed(LlamaServer *srv, const QString &model, const QString &error);
    void startDialogFromSettings();
    void startAnalyzerFromSettings();
    void onServerText(LlamaServer *srv, const QString &text);
    void onServerGenFail(LlamaServer *srv, const QString &error);

    enum class PendingKind { None, Dialog, Analyzer };

    LlamaServer *m_dialog = nullptr;
    LlamaServer *m_analyzer = nullptr;
    bool m_shared = false;
    PendingKind m_pending = PendingKind::None;
};

#endif // LOCALLLM_H
