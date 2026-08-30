#ifndef DIALOGENGINE_H
#define DIALOGENGINE_H

#include "core/types.h"

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

class LocalLlm;
class QTimer;

class DialogEngine : public QObject
{
    Q_OBJECT

public:
    explicit DialogEngine(LocalLlm *llm, QObject *parent = nullptr);

    bool isBusy() const { return m_busy; }
    void start(const CatalogItem &product,
               const CatalogItem &buyer,
               const QString &sellerPrompt,
               const QString &buyerPrompt,
               const QString &seriesId,
               int cycleIndex,
               int targetPairs = -1);
    void abort();
    AnalysisRecord finishEarly();

signals:
    void turnReady(const DialogTurn &turn);
    void progress(int pair, int target, const QString &status);
    void finished(const AnalysisRecord &record);
    void failed(const QString &message);
    void aborted();

private:
    enum class Phase { Idle, WaitServers, Seller, Buyer };

    void beginWhenReady();
    void requestSeller();
    void requestBuyer();
    void onDialogReply(const QString &text);
    void onDialogFailed(const QString &error);
    void completeOk();
    void recountPairs();
    void startTurnClock(int pair, int target, const QString &base);
    void stopTurnClock();
    void emitTurnWait();
    QVector<ChatMessage> sellerHistory() const;
    QVector<ChatMessage> buyerHistory() const;
    static QVector<ChatMessage> withLanguageLock(QVector<ChatMessage> hist, bool retry);

    LocalLlm *m_llm = nullptr;
    bool m_busy = false;
    bool m_stopping = false;
    Phase m_phase = Phase::Idle;
    AnalysisRecord m_record;
    QString m_sellerSystem;
    QString m_buyerSystem;
    int m_targetPairs = 5;
    QElapsedTimer m_timer;
    QTimer *m_turnTick = nullptr;
    QElapsedTimer m_turnClock;
    int m_turnEstimateSec = 5;
    int m_progPair = 0;
    int m_progTarget = 14;
    QString m_baseProgress;
    int m_langRetries = 0;
};

#endif // DIALOGENGINE_H
