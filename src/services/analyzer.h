#ifndef ANALYZER_H
#define ANALYZER_H

#include "core/types.h"

#include <QElapsedTimer>
#include <QObject>

class LocalLlm;
class QTimer;

class Analyzer : public QObject
{
    Q_OBJECT

public:
    explicit Analyzer(LocalLlm *llm, QObject *parent = nullptr);

    bool isBusy() const { return m_busy; }
    void analyze(const AnalysisRecord &dialog);
    void abort();

    static AnalysisRecord parseModelOutput(const AnalysisRecord &dialog, const QString &raw);

signals:
    void finished(const AnalysisRecord &record);
    void failed(const QString &message);
    void progress(const QString &status);

private:
    void onReply(const QString &text);
    void onFailed(const QString &error);
    void startWaitClock();
    void stopWaitClock();
    void emitWaitProgress();
    static QString systemPrompt();
    static QString userPayload(const AnalysisRecord &dialog);

    LocalLlm *m_llm = nullptr;
    bool m_busy = false;
    AnalysisRecord m_pending;
    QTimer *m_waitTick = nullptr;
    QElapsedTimer m_waitClock;
    int m_estimateSec = 8;
    QString m_baseStatus;
};

#endif // ANALYZER_H
