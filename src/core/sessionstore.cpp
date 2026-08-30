#include "sessionstore.h"

#include "apppaths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

bool hasCjk(const QString &s)
{
    for (const QChar c : s) {
        const uint u = c.unicode();
        if ((u >= 0x3040 && u <= 0x30FF) || (u >= 0x3400 && u <= 0x4DBF)
            || (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0xF900 && u <= 0xFAFF)
            || (u >= 0xAC00 && u <= 0xD7AF))
            return true;
    }
    return false;
}

bool isJunkText(const QString &s)
{
    if (s.isEmpty())
        return false;
    if (hasCjk(s))
        return true;
    if (s.contains(QStringLiteral("Не удалось разобрать"))
        || s.contains(QStringLiteral("не разобран как JSON")))
        return true;
    const QString t = s.trimmed();
    return t.startsWith(QLatin1Char('{')) && t.contains(QStringLiteral("\"scores\""));
}

bool isBrokenAnalysis(const AnalysisRecord &r)
{
    for (int i = 0; i < 5; ++i) {
        if (isJunkText(r.scores.mistakeAt(i)) || isJunkText(r.scores.recommendationAt(i)))
            return true;
    }
    for (const QString &s : r.mistakes) {
        if (isJunkText(s))
            return true;
    }
    for (const QString &s : r.recommendations) {
        if (isJunkText(s))
            return true;
    }
    return isJunkText(r.newPrompt);
}

} // namespace

SessionStore &SessionStore::instance()
{
    static SessionStore s;
    return s;
}

SessionStore::SessionStore(QObject *parent)
    : QObject(parent)
{
}

static void putNote(QJsonObject &o, const QString &key, const QString &mistake, const QString &rec)
{
    QJsonObject n;
    n.insert(QStringLiteral("mistake"), mistake);
    n.insert(QStringLiteral("recommendation"), rec);
    o.insert(key, n);
}

static void readNote(const QJsonObject &notes, const QString &key, QString *mistake, QString *rec)
{
    const QJsonObject n = notes.value(key).toObject();
    *mistake = n.value(QStringLiteral("mistake")).toString();
    *rec = n.value(QStringLiteral("recommendation")).toString();
}

static QJsonObject scoresToJson(const ScoreSet &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("contact"), s.contact);
    o.insert(QStringLiteral("needs"), s.needs);
    o.insert(QStringLiteral("objections"), s.objections);
    o.insert(QStringLiteral("offer"), s.offer);
    o.insert(QStringLiteral("buyer_fit"), s.buyerFit);
    QJsonObject notes;
    putNote(notes, QStringLiteral("contact"), s.contactMistake, s.contactRecommendation);
    putNote(notes, QStringLiteral("needs"), s.needsMistake, s.needsRecommendation);
    putNote(notes, QStringLiteral("objections"), s.objectionsMistake, s.objectionsRecommendation);
    putNote(notes, QStringLiteral("offer"), s.offerMistake, s.offerRecommendation);
    putNote(notes, QStringLiteral("buyer_fit"), s.buyerFitMistake, s.buyerFitRecommendation);
    o.insert(QStringLiteral("notes"), notes);
    return o;
}

static ScoreSet scoresFromJson(const QJsonObject &o)
{
    ScoreSet s;
    s.contact = o.value(QStringLiteral("contact")).toInt();
    s.needs = o.value(QStringLiteral("needs")).toInt();
    s.objections = o.value(QStringLiteral("objections")).toInt();
    s.offer = o.value(QStringLiteral("offer")).toInt();
    s.buyerFit = o.value(QStringLiteral("buyer_fit")).toInt(o.value(QStringLiteral("buyerFit")).toInt());
    const QJsonObject notes = o.value(QStringLiteral("notes")).toObject();
    readNote(notes, QStringLiteral("contact"), &s.contactMistake, &s.contactRecommendation);
    readNote(notes, QStringLiteral("needs"), &s.needsMistake, &s.needsRecommendation);
    readNote(notes, QStringLiteral("objections"), &s.objectionsMistake, &s.objectionsRecommendation);
    readNote(notes, QStringLiteral("offer"), &s.offerMistake, &s.offerRecommendation);
    readNote(notes, QStringLiteral("buyer_fit"), &s.buyerFitMistake, &s.buyerFitRecommendation);
    return s;
}

QJsonObject SessionStore::toJson(const AnalysisRecord &r)
{
    QJsonObject o;
    o.insert(QStringLiteral("dialogId"), r.dialogId);
    o.insert(QStringLiteral("seriesId"), r.seriesId);
    o.insert(QStringLiteral("cycleIndex"), r.cycleIndex);
    o.insert(QStringLiteral("productItem"), r.productItem);
    o.insert(QStringLiteral("productDescr"), r.productDescr);
    o.insert(QStringLiteral("buyerType"), r.buyerType);
    o.insert(QStringLiteral("buyerDescr"), r.buyerDescr);
    o.insert(QStringLiteral("sellerPrompt"), r.sellerPrompt);
    o.insert(QStringLiteral("sellerPromptTemplate"), r.sellerPromptTemplate);
    o.insert(QStringLiteral("pairCount"), r.pairCount);
    o.insert(QStringLiteral("elapsedMs"), r.elapsedMs);
    o.insert(QStringLiteral("scores"), scoresToJson(r.scores));
    o.insert(QStringLiteral("average"), r.average);
    o.insert(QStringLiteral("mistakes"), QJsonArray::fromStringList(r.mistakes));
    o.insert(QStringLiteral("recommendations"), QJsonArray::fromStringList(r.recommendations));
    o.insert(QStringLiteral("newPrompt"), r.newPrompt);
    o.insert(QStringLiteral("delta"), r.delta);
    o.insert(QStringLiteral("hasDelta"), r.hasDelta);

    QJsonArray turns;
    for (const DialogTurn &t : r.transcript) {
        QJsonObject to;
        to.insert(QStringLiteral("speaker"), t.speaker);
        to.insert(QStringLiteral("text"), t.text);
        turns.append(to);
    }
    o.insert(QStringLiteral("transcript"), turns);
    return o;
}

AnalysisRecord SessionStore::fromJson(const QJsonObject &o)
{
    AnalysisRecord r;
    r.dialogId = o.value(QStringLiteral("dialogId")).toInt();
    r.seriesId = o.value(QStringLiteral("seriesId")).toString();
    r.cycleIndex = o.value(QStringLiteral("cycleIndex")).toInt();
    r.productItem = o.value(QStringLiteral("productItem")).toString();
    r.productDescr = o.value(QStringLiteral("productDescr")).toString();
    r.buyerType = o.value(QStringLiteral("buyerType")).toString();
    r.buyerDescr = o.value(QStringLiteral("buyerDescr")).toString();
    r.sellerPrompt = o.value(QStringLiteral("sellerPrompt")).toString();
    r.sellerPromptTemplate = o.value(QStringLiteral("sellerPromptTemplate")).toString();
    r.pairCount = o.value(QStringLiteral("pairCount")).toInt();
    r.elapsedMs = qint64(o.value(QStringLiteral("elapsedMs")).toDouble());
    r.scores = scoresFromJson(o.value(QStringLiteral("scores")).toObject());
    r.average = o.value(QStringLiteral("average")).toDouble();
    r.newPrompt = o.value(QStringLiteral("newPrompt")).toString();
    r.delta = o.value(QStringLiteral("delta")).toDouble();
    r.hasDelta = o.value(QStringLiteral("hasDelta")).toBool();

    for (const QJsonValue &v : o.value(QStringLiteral("mistakes")).toArray())
        r.mistakes << v.toString();
    for (const QJsonValue &v : o.value(QStringLiteral("recommendations")).toArray())
        r.recommendations << v.toString();
    for (int i = 0; i < 5; ++i) {
        if (r.scores.mistakeAt(i).isEmpty() && i < r.mistakes.size())
            r.scores.setMistakeAt(i, r.mistakes.at(i));
        if (r.scores.recommendationAt(i).isEmpty() && i < r.recommendations.size())
            r.scores.setRecommendationAt(i, r.recommendations.at(i));
    }
    for (const QJsonValue &v : o.value(QStringLiteral("transcript")).toArray()) {
        const QJsonObject to = v.toObject();
        DialogTurn t;
        t.speaker = to.value(QStringLiteral("speaker")).toString();
        t.text = to.value(QStringLiteral("text")).toString();
        r.transcript.push_back(t);
    }
    return r;
}

bool SessionStore::load()
{
    m_error.clear();
    m_records.clear();
    m_nextDialogId = 1;
    const QString path = AppPaths::resultsFilePath();
    QFile f(path);
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Не удалось прочитать %1").arg(path);
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_error = QStringLiteral("Повреждён results.json: %1").arg(err.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    m_nextDialogId = qMax(1, root.value(QStringLiteral("nextDialogId")).toInt(1));
    bool droppedJunk = false;
    for (const QJsonValue &v : root.value(QStringLiteral("results")).toArray()) {
        if (!v.isObject())
            continue;
        AnalysisRecord rec = fromJson(v.toObject());
        if (isUnusable(rec)) {
            droppedJunk = true;
            continue;
        }
        m_records.push_back(rec);
    }
    recomputeDeltas();
    if (droppedJunk)
        save();
    emit recordsChanged();
    return true;
}

bool SessionStore::save() const
{
    m_error.clear();
    QJsonObject root;
    root.insert(QStringLiteral("nextDialogId"), m_nextDialogId);
    QJsonArray arr;
    for (const AnalysisRecord &r : m_records)
        arr.append(toJson(r));
    root.insert(QStringLiteral("results"), arr);

    const QString path = AppPaths::resultsFilePath();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = QStringLiteral("Не удалось записать %1").arg(path);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

int SessionStore::nextDialogId()
{
    return m_nextDialogId++;
}

void SessionStore::clear()
{
    m_records.clear();
    m_nextDialogId = 1;
    save();
    emit recordsChanged();
}

void SessionStore::recomputeDeltas()
{
    for (int i = 0; i < m_records.size(); ++i) {
        if (i == 0) {
            m_records[i].hasDelta = false;
            m_records[i].delta = 0.0;
            continue;
        }
        m_records[i].hasDelta = true;
        m_records[i].delta = m_records[i].average - m_records[i - 1].average;
    }
}

void SessionStore::updateDeltas(AnalysisRecord *record) const
{
    if (!record)
        return;
    record->hasDelta = false;
    record->delta = 0.0;
    if (m_records.isEmpty())
        return;
    record->hasDelta = true;
    record->delta = record->average - m_records.last().average;
}

bool SessionStore::isUnusable(const AnalysisRecord &r)
{
    if (r.dialogId <= 0)
        return true;
    if (isBrokenAnalysis(r))
        return true;
    return r.scores.contact <= 0 && r.scores.needs <= 0 && r.scores.objections <= 0
        && r.scores.offer <= 0 && r.scores.buyerFit <= 0;
}

bool SessionStore::append(const AnalysisRecord &record)
{
    if (isUnusable(record))
        return false;
    AnalysisRecord copy = record;
    updateDeltas(&copy);
    m_records.push_back(copy);
    save();
    emit recordsChanged();
    return true;
}

AnalysisRecord SessionStore::recordById(int dialogId) const
{
    for (const AnalysisRecord &r : m_records) {
        if (r.dialogId == dialogId)
            return r;
    }
    return {};
}

AnalysisRecord SessionStore::lastRecord() const
{
    if (m_records.isEmpty())
        return {};
    return m_records.last();
}
