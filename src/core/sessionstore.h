#ifndef SESSIONSTORE_H
#define SESSIONSTORE_H

#include "types.h"

#include <QObject>
#include <QVector>

class SessionStore : public QObject
{
    Q_OBJECT

public:
    static SessionStore &instance();

    bool load();
    bool save() const;
    QString lastError() const { return m_error; }

    int nextDialogId();
    void clear();
    bool append(const AnalysisRecord &record);
    void updateDeltas(AnalysisRecord *record) const;
    void recomputeDeltas();
    static bool isUnusable(const AnalysisRecord &record);

    const QVector<AnalysisRecord> &records() const { return m_records; }
    AnalysisRecord recordById(int dialogId) const;
    AnalysisRecord lastRecord() const;

signals:
    void recordsChanged();

private:
    explicit SessionStore(QObject *parent = nullptr);
    Q_DISABLE_COPY(SessionStore)

    static QJsonObject toJson(const AnalysisRecord &r);
    static AnalysisRecord fromJson(const QJsonObject &o);

    QVector<AnalysisRecord> m_records;
    int m_nextDialogId = 1;
    mutable QString m_error;
};

#endif // SESSIONSTORE_H
