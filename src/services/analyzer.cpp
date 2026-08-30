#include "analyzer.h"

#include "core/apppaths.h"
#include "core/appsettings.h"
#include "core/catalogs.h"
#include "localllm.h"

#include <QTimer>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

int clampScore(int v)
{
    return qBound(1, v, 10);
}

int pickScore(const QJsonObject &scores, const QStringList &keys)
{
    for (const QString &k : keys) {
        if (scores.contains(k)) {
            const QJsonValue v = scores.value(k);
            if (v.isDouble())
                return clampScore(int(qRound(v.toDouble())));
            if (v.isString()) {
                bool ok = false;
                const int n = v.toString().trimmed().toInt(&ok);
                if (ok)
                    return clampScore(n);
            }
        }
    }
    return 0;
}

QString repairJsonSlice(QString s)
{
    bool inString = false;
    bool escape = false;
    int braces = 0;
    int brackets = 0;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (escape) {
            escape = false;
            continue;
        }
        if (c == QLatin1Char('\\') && inString) {
            escape = true;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inString = !inString;
            continue;
        }
        if (inString)
            continue;
        if (c == QLatin1Char('{'))
            ++braces;
        else if (c == QLatin1Char('}'))
            --braces;
        else if (c == QLatin1Char('['))
            ++brackets;
        else if (c == QLatin1Char(']'))
            --brackets;
    }
    if (inString)
        s += QLatin1Char('"');
    while (s.endsWith(QLatin1Char(',')) || s.endsWith(QLatin1Char(' '))
           || s.endsWith(QLatin1Char('\n')) || s.endsWith(QLatin1Char('\t'))) {
        if (s.endsWith(QLatin1Char(',')))
            s.chop(1);
        else
            s = s.trimmed();
    }
    while (brackets > 0) {
        s += QLatin1Char(']');
        --brackets;
    }
    while (braces > 0) {
        s += QLatin1Char('}');
        --braces;
    }
    return s;
}

int readLooseScore(const QString &text, const QStringList &keys)
{
    for (const QString &k : keys) {
        const QRegularExpression re(
            QStringLiteral("(?:\"%1\"|\\b%1\\b)\\s*[:=]\\s*\"?(\\d{1,2})\"?")
                .arg(QRegularExpression::escape(k)),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = re.match(text);
        if (m.hasMatch())
            return clampScore(m.captured(1).toInt());
    }
    return 0;
}

QJsonObject scoresFromLooseText(const QString &text)
{
    QJsonObject scores;
    const int contact = readLooseScore(text, {QStringLiteral("contact"), QStringLiteral("контакт")});
    const int needs = readLooseScore(text, {QStringLiteral("needs"), QStringLiteral("потребность")});
    const int objections = readLooseScore(text, {QStringLiteral("objections"), QStringLiteral("возражения")});
    const int offer = readLooseScore(text, {QStringLiteral("offer"), QStringLiteral("оффер")});
    const int buyer = readLooseScore(text, {QStringLiteral("buyer_fit"), QStringLiteral("buyerFit"),
                                           QStringLiteral("типаж")});
    int found = 0;
    if (contact) { scores.insert(QStringLiteral("contact"), contact); ++found; }
    if (needs) { scores.insert(QStringLiteral("needs"), needs); ++found; }
    if (objections) { scores.insert(QStringLiteral("objections"), objections); ++found; }
    if (offer) { scores.insert(QStringLiteral("offer"), offer); ++found; }
    if (buyer) { scores.insert(QStringLiteral("buyer_fit"), buyer); ++found; }
    if (found < 3)
        return {};
    return scores;
}

QStringList toStringList(const QJsonValue &v)
{
    QStringList out;
    if (v.isArray()) {
        for (const QJsonValue &x : v.toArray()) {
            const QString s = x.toString().trimmed();
            if (!s.isEmpty())
                out << s;
        }
    } else if (v.isString()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}

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

QString stripCjk(QString s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        const uint u = c.unicode();
        if ((u >= 0x3040 && u <= 0x30FF) || (u >= 0x3400 && u <= 0x4DBF)
            || (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0xF900 && u <= 0xFAFF)
            || (u >= 0xAC00 && u <= 0xD7AF))
            continue;
        out += c;
    }
    return out.simplified();
}

QString cleanLine(QString s)
{
    if (hasCjk(s))
        s = stripCjk(s);
    return s.trimmed();
}

QStringList keepRussianLines(const QStringList &in)
{
    QStringList out;
    for (const QString &s : in) {
        const QString t = cleanLine(s);
        if (t.size() >= 4)
            out << t;
    }
    return out;
}

void readCriterionNote(const QJsonObject &criteria, const QStringList &keys,
                       QString *mistake, QString *rec)
{
    QJsonObject n;
    for (const QString &k : keys) {
        if (criteria.value(k).isObject()) {
            n = criteria.value(k).toObject();
            break;
        }
    }
    *mistake = cleanLine(n.value(QStringLiteral("mistake")).toString());
    if (mistake->isEmpty())
        *mistake = cleanLine(n.value(QStringLiteral("error")).toString());
    *rec = cleanLine(n.value(QStringLiteral("recommendation")).toString());
    if (rec->isEmpty())
        *rec = cleanLine(n.value(QStringLiteral("advice")).toString());
}

QJsonObject extractJsonObject(const QString &text)
{
    QString t = text;
    const QRegularExpression fence(QStringLiteral(R"(```(?:json)?\s*([\s\S]*?)```)"),
                                   QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = fence.match(t);
    if (m.hasMatch())
        t = m.captured(1);

    auto tryParse = [](const QString &slice) -> QJsonObject {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(slice.trimmed().toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
        return {};
    };

    QJsonObject obj = tryParse(t);
    if (!obj.isEmpty())
        return obj;

    const int start = t.indexOf(QLatin1Char('{'));
    if (start >= 0) {
        const int end = t.lastIndexOf(QLatin1Char('}'));
        if (end > start) {
            obj = tryParse(t.mid(start, end - start + 1));
            if (!obj.isEmpty())
                return obj;
        }
        obj = tryParse(repairJsonSlice(t.mid(start)));
        if (!obj.isEmpty())
            return obj;
    }

    const QJsonObject loose = scoresFromLooseText(text);
    if (!loose.isEmpty()) {
        QJsonObject wrap;
        wrap.insert(QStringLiteral("scores"), loose);
        wrap.insert(QStringLiteral("partial"), true);
        return wrap;
    }
    return {};
}

} // namespace

Analyzer::Analyzer(LocalLlm *llm, QObject *parent)
    : QObject(parent)
    , m_llm(llm)
{
    connect(m_llm, &LocalLlm::analyzerReply, this, &Analyzer::onReply);
    connect(m_llm, &LocalLlm::analyzerFailed, this, &Analyzer::onFailed);
    m_waitTick = new QTimer(this);
    m_waitTick->setInterval(1000);
    connect(m_waitTick, &QTimer::timeout, this, &Analyzer::emitWaitProgress);
}

void Analyzer::abort()
{
    if (!m_busy)
        return;
    stopWaitClock();
    m_busy = false;
    m_llm->abortPendingGeneration();
}

void Analyzer::startWaitClock()
{
    const auto &cfg = AppSettings::instance();
    int ngl = cfg.analyzerGpuLayers();
    if (m_llm->usesSharedServer() && m_llm->dialogServer() && m_llm->dialogServer()->gpuLayers() >= 0)
        ngl = m_llm->dialogServer()->gpuLayers();
    else if (m_llm->analyzerServer() && m_llm->analyzerServer()->gpuLayers() >= 0)
        ngl = m_llm->analyzerServer()->gpuLayers();
    const QString model = m_llm->analyzerModelName().isEmpty() ? cfg.analyzerModel()
                                                              : m_llm->analyzerModelName();
    m_estimateSec = AppPaths::estimateGenerationSec(model, ngl, cfg.analyzerMaxTokens());
    m_waitClock.restart();
    m_waitTick->start();
    emitWaitProgress();
}

void Analyzer::stopWaitClock()
{
    if (m_waitTick)
        m_waitTick->stop();
}

void Analyzer::emitWaitProgress()
{
    if (!m_busy)
        return;
    emit progress(m_baseStatus + QLatin1Char(' ')
                  + AppPaths::formatWaitHint(m_waitClock.elapsed(), m_estimateSec));
}

QString Analyzer::systemPrompt()
{
    return QStringLiteral(
        "Методист по продажам. Оцени стенограмму. Ответ — только JSON, язык строк — русский, без иероглифов.\n"
        "Схема (коротко, без лишних полей и без нового промпта продавца):\n"
        "{\"scores\":{\"contact\":n,\"needs\":n,\"objections\":n,\"offer\":n,\"buyer_fit\":n},"
        "\"mistakes\":[\"...\",\"...\"],\"recommendations\":[\"...\",\"...\"]}\n"
        "n = целое 1..10. contact=доверие, needs=потребность, objections=возражения, "
        "offer=цена/срок/шаг, buyer_fit=типаж.\n"
        "mistakes и recommendations — по 3-5 коротких фраз на русском, в том же порядке критериев. "
        "После } ничего не пиши.");
}

QString Analyzer::userPayload(const AnalysisRecord &dialog)
{
    QString transcript;
    for (const DialogTurn &t : dialog.transcript) {
        const QString who = t.speaker == QStringLiteral("seller")
            ? QStringLiteral("Продавец")
            : QStringLiteral("Покупатель");
        QString line = t.text.simplified();
        if (line.size() > 280)
            line = line.left(277) + QStringLiteral("…");
        transcript += who + QStringLiteral(": ") + line + QStringLiteral("\n");
    }
    return QStringLiteral(
               "Товар: %1\nПокупатель: %2 (%3)\nПар: %4\n\nСтенограмма:\n%5")
        .arg(dialog.productItem, dialog.buyerType, dialog.buyerDescr)
        .arg(dialog.pairCount)
        .arg(transcript);
}

AnalysisRecord Analyzer::parseModelOutput(const AnalysisRecord &dialog, const QString &raw)
{
    AnalysisRecord r = dialog;
    const QJsonObject obj = extractJsonObject(raw);
    const QJsonObject scores = obj.value(QStringLiteral("scores")).toObject();
    r.scores.contact = pickScore(scores, {QStringLiteral("contact"), QStringLiteral("контакт")});
    r.scores.needs = pickScore(scores, {QStringLiteral("needs"), QStringLiteral("потребность")});
    r.scores.objections = pickScore(scores, {QStringLiteral("objections"), QStringLiteral("возражения")});
    r.scores.offer = pickScore(scores, {QStringLiteral("offer"), QStringLiteral("оффер")});
    r.scores.buyerFit = pickScore(scores, {QStringLiteral("buyer_fit"), QStringLiteral("buyerFit"), QStringLiteral("типаж")});
    r.average = obj.value(QStringLiteral("average")).toDouble(r.scores.average());
    if (r.average <= 0.0)
        r.average = r.scores.average();

    QJsonObject criteria = obj.value(QStringLiteral("criteria")).toObject();
    if (criteria.isEmpty())
        criteria = obj.value(QStringLiteral("notes")).toObject();
    readCriterionNote(criteria, {QStringLiteral("contact"), QStringLiteral("контакт")},
                      &r.scores.contactMistake, &r.scores.contactRecommendation);
    readCriterionNote(criteria, {QStringLiteral("needs"), QStringLiteral("потребность")},
                      &r.scores.needsMistake, &r.scores.needsRecommendation);
    readCriterionNote(criteria, {QStringLiteral("objections"), QStringLiteral("возражения")},
                      &r.scores.objectionsMistake, &r.scores.objectionsRecommendation);
    readCriterionNote(criteria, {QStringLiteral("offer"), QStringLiteral("оффер")},
                      &r.scores.offerMistake, &r.scores.offerRecommendation);
    readCriterionNote(criteria, {QStringLiteral("buyer_fit"), QStringLiteral("buyerFit"), QStringLiteral("типаж")},
                      &r.scores.buyerFitMistake, &r.scores.buyerFitRecommendation);

    const QStringList flatMistakes = keepRussianLines(toStringList(obj.value(QStringLiteral("mistakes"))));
    const QStringList flatRecs = keepRussianLines(toStringList(obj.value(QStringLiteral("recommendations"))));
    for (int i = 0; i < 5; ++i) {
        if (r.scores.mistakeAt(i).isEmpty() && i < flatMistakes.size())
            r.scores.setMistakeAt(i, flatMistakes.at(i));
        if (r.scores.recommendationAt(i).isEmpty() && i < flatRecs.size())
            r.scores.setRecommendationAt(i, flatRecs.at(i));
    }

    r.newPrompt = obj.value(QStringLiteral("new_prompt")).toString().trimmed();
    if (r.newPrompt.isEmpty())
        r.newPrompt = obj.value(QStringLiteral("newPrompt")).toString().trimmed();
    if (hasCjk(r.newPrompt)) {
        if (r.scores.contactRecommendation.isEmpty()) {
            r.scores.contactRecommendation = QStringLiteral(
                "Модель предложила промпт на китайском — оставлен русский исходный.");
        }
        r.newPrompt.clear();
    }
    r.mistakes.clear();
    r.recommendations.clear();
    for (int i = 0; i < 5; ++i) {
        if (!r.scores.mistakeAt(i).isEmpty())
            r.mistakes << r.scores.mistakeAt(i);
        if (!r.scores.recommendationAt(i).isEmpty())
            r.recommendations << r.scores.recommendationAt(i);
    }
    if (r.newPrompt.isEmpty() || !Catalogs::hasBuyerPlaceholder(r.newPrompt)) {
        QString extra = r.recommendations.join(QStringLiteral("\n- "));
        r.newPrompt = Catalogs::keepAsSellerTemplate(
            dialog.sellerPromptTemplate, AppSettings::instance().sellerPrompt());
        if (!extra.isEmpty())
            r.newPrompt += QStringLiteral("\n\nДополнительные правила по итогам разбора:\n- ") + extra;
    }
    if (obj.isEmpty()) {
        r.scores = ScoreSet();
        r.average = 0.0;
        QString snippet = raw.simplified();
        if (snippet.size() > 180)
            snippet = snippet.left(177) + QStringLiteral("…");
        r.scores.setMistakeAt(0, QStringLiteral(
            "Ответ анализатора не разобран как JSON — оценки не выставлялись. "
            "На слабом железе модель часто обрывает длинный ответ. Фрагмент: %1")
                                     .arg(snippet.isEmpty() ? QStringLiteral("(пусто)") : snippet));
        r.newPrompt = Catalogs::keepAsSellerTemplate(
            dialog.sellerPromptTemplate, AppSettings::instance().sellerPrompt());
        r.mistakes = {r.scores.contactMistake};
        r.recommendations.clear();
    } else if (obj.value(QStringLiteral("partial")).toBool()) {
        if (r.scores.contactMistake.isEmpty()) {
            r.scores.contactMistake = QStringLiteral(
                "JSON был неполным — оценки сняты из текста, комментарии могли потеряться.");
            r.mistakes = {r.scores.contactMistake};
        }
    }
    return r;
}

void Analyzer::analyze(const AnalysisRecord &dialog)
{
    if (m_busy)
        return;
    m_pending = dialog;
    m_busy = true;
    m_baseStatus = QStringLiteral("Анализ диалога #%1 моделью %2 (%3)…")
                       .arg(dialog.dialogId)
                       .arg(m_llm->analyzerModelName().isEmpty()
                                ? AppSettings::instance().analyzerModel()
                                : m_llm->analyzerModelName(),
                            m_llm->analyzerDeviceLabel());
    startWaitClock();

    auto kick = [this]() {
        if (!m_busy)
            return;
        QVector<ChatMessage> hist;
        hist.push_back({QStringLiteral("user"), userPayload(m_pending)});
        m_llm->generateAnalyzer(systemPrompt(), hist);
    };

    if (m_llm->isAnalyzerReady()) {
        kick();
    } else {
        QObject *ctx = new QObject(this);
        connect(m_llm, &LocalLlm::analyzerServerReady, ctx, [this, ctx, kick]() {
            ctx->deleteLater();
            kick();
        });
        m_llm->ensureAnalyzer();
    }
}

void Analyzer::onReply(const QString &text)
{
    if (!m_busy)
        return;
    stopWaitClock();
    m_busy = false;
    emit finished(parseModelOutput(m_pending, text));
}

void Analyzer::onFailed(const QString &error)
{
    if (!m_busy)
        return;
    stopWaitClock();
    m_busy = false;
    emit failed(error);
}
