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
    const QRegularExpression fence(
        QStringLiteral(R"(```(?:json)?[ \t\r\n]*((?s).*?)```)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
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

const QString kPriorityBegin = QStringLiteral("[ПРИОРИТЕТ — важнее текста ниже]");
const QString kPriorityEnd = QStringLiteral("[/ПРИОРИТЕТ]");

QString coreSellerTemplate(const QString &templ)
{
    const int end = templ.indexOf(kPriorityEnd);
    if (end < 0)
        return templ.trimmed();
    return templ.mid(end + kPriorityEnd.size()).trimmed();
}

QStringList uniqueNonEmpty(const QStringList &in)
{
    QStringList out;
    for (const QString &raw : in) {
        const QString t = raw.trimmed();
        if (t.isEmpty())
            continue;
        bool seen = false;
        for (const QString &e : out) {
            if (QString::compare(e, t, Qt::CaseInsensitive) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen)
            out << t;
    }
    return out;
}

bool looksBuyerFacing(const QString &s)
{
    const QString t = s.toLower();
    return t.contains(QStringLiteral("покупателю"))
        || t.contains(QStringLiteral("покупатель должен"))
        || t.contains(QStringLiteral("покупатель не"))
        || t.contains(QStringLiteral("клиенту нужно"))
        || t.contains(QStringLiteral("клиент должен"))
        || t.contains(QStringLiteral("клиент не дает"))
        || t.contains(QStringLiteral("клиент не даёт"));
}

QString fallbackMistakeFor(int criterion, int score)
{
    if (score <= 0 || score > 7)
        return {};
    switch (criterion) {
    case 0:
        return QStringLiteral("Контакт слабый: повторные приветствия или нет опоры на задачу клиента.");
    case 1:
        return QStringLiteral("Потребность не выяснена: мало уточняющих вопросов, сразу предложение.");
    case 2:
        return QStringLiteral("Возражения закрыты общими фразами, без фактов из базы знаний.");
    case 3:
        return QStringLiteral("Оффер размыт: нет цены, срока или конкретного следующего шага из базы.");
    case 4:
        return QStringLiteral("Тон не подстроен под типаж покупателя — один шаблон на всех.");
    default:
        return {};
    }
}

QString goodBehaviorRule(int criterion)
{
    switch (criterion) {
    case 0:
        return QStringLiteral("Поздоровайтесь только в первой реплике. Дальше не пишите «Здравствуйте» и «уважаемый клиент».");
    case 1:
        return QStringLiteral("Сначала задайте 1–2 вопроса о задаче клиента. Не предлагайте купить или оплатить, пока потребность не ясна.");
    case 2:
        return QStringLiteral("На сомнения отвечайте только фактами из базы знаний. Не выдумывайте скидки и фразу «лучшее на рынке».");
    case 3:
        return QStringLiteral("Называйте только цены и сроки из базы. Предложите демо, расчёт или пробный период — не оплату сразу.");
    case 4:
        return QStringLiteral("Подстройте тон под {buyer_type} и {buyer_descr}. Не говорите со всеми одним шаблоном.");
    default:
        return {};
    }
}

QString fallbackRecFor(int criterion, int score)
{
    if (score <= 0 || score > 7)
        return {};
    return goodBehaviorRule(criterion);
}

QStringList rulesFromScores(const ScoreSet &s)
{
    QStringList o;
    for (int i = 0; i < 5; ++i) {
        const QString rec = fallbackRecFor(i, s.valueAt(i));
        if (!rec.isEmpty())
            o << rec;
    }
    return o;
}

int weakDemoHits(const QString &core)
{
    const QString t = core.toLower();
    int n = 0;
    const QStringList marks = {
        QStringLiteral("тороплив"),
        QStringLiteral("давите на оплату"),
        QStringLiteral("лучшее на рынке"),
        QStringLiteral("опираться на неё слабо"),
        QStringLiteral("опираться на нее слабо"),
        QStringLiteral("придумайте цену"),
        QStringLiteral("можете не учитывать"),
        QStringLiteral("акцию только сегодня"),
    };
    for (const QString &m : marks) {
        if (t.contains(m))
            ++n;
    }
    return n;
}

QString closingLine(const QString &core)
{
    static const QRegularExpression re(
        QStringLiteral("Отвечайте[^\\n]*по-русски[^\\n]*"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(core);
    QString last;
    while (it.hasNext())
        last = it.next().captured().trimmed();
    if (!last.isEmpty())
        return last;
    return QStringLiteral("Отвечайте по-русски, 4–10 предложений.");
}

QString applyBehaviorRules(const QString &core, const QStringList &rules)
{
    const QString block = QStringLiteral("Как вести себя:\n- ") + rules.join(QStringLiteral("\n- "));
    static const QRegularExpression re(
        QStringLiteral("Как вести себя\\s*:[\\s\\S]*?(?=\\nОтвечайте|\\z)"),
        QRegularExpression::CaseInsensitiveOption);
    if (re.match(core).hasMatch()) {
        QString out = core;
        out.replace(re, block + QLatin1Char('\n'));
        return out.trimmed();
    }
    return core.trimmed() + QStringLiteral("\n\n") + block;
}

QString rewriteWeakDemoCore(const QStringList &rules, const QString &oldCore)
{
    return QStringLiteral(
               "Вы продавец. Сначала выясните задачу клиента, затем опирайтесь на справку. "
               "Не закрывайте оплату в первой реплике.\n\n"
               "Товар: {item}\n"
               "Описание: {item_descr}\n\n"
               "Ниже справка. Называйте только цены, сроки и условия из неё. Не выдумывайте скидки и акции.\n"
               "{item_knowledge}\n\n"
               "Тип покупателя: {buyer_type}\n"
               "Заметки о нём: {buyer_descr}\n"
               "Подстройте тон и вопросы под тип и заметки.\n\n"
               "Как вести себя:\n- %1\n\n"
               "%2")
        .arg(rules.join(QStringLiteral("\n- ")), closingLine(oldCore));
}

QString buildNextSellerPrompt(const QString &usedTemplate, const ScoreSet &scores,
                              const QStringList &recommendations)
{
    const QString raw = Catalogs::keepAsSellerTemplate(
        usedTemplate, AppSettings::instance().sellerPrompt());
    const QString core = coreSellerTemplate(raw);
    QStringList rules = rulesFromScores(scores);
    rules << recommendations;
    rules = uniqueNonEmpty(rules);
    if (rules.isEmpty() && weakDemoHits(core) >= 2) {
        for (int i = 0; i < 5; ++i)
            rules << goodBehaviorRule(i);
        rules = uniqueNonEmpty(rules);
    }
    if (rules.isEmpty())
        return raw.trimmed();
    if (rules.size() > 8)
        rules = rules.mid(0, 8);
    if (weakDemoHits(core) >= 2)
        return rewriteWeakDemoCore(rules, core);
    return applyBehaviorRules(core, rules);
}

void capScore(int *v, int max)
{
    if (v && *v > max)
        *v = max;
}

int extraSellerGreetings(const QVector<DialogTurn> &turns)
{
    static const QRegularExpression greet(
        QStringLiteral("^(?:здравствуйте|добрый\\s+(?:день|вечер|утро)|приветствую|привет)[\\s!,.]"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    int extra = 0;
    bool seenSeller = false;
    for (const DialogTurn &t : turns) {
        if (t.speaker != QLatin1String("seller"))
            continue;
        if (greet.match(t.text.trimmed()).hasMatch() && seenSeller)
            ++extra;
        seenSeller = true;
    }
    return extra;
}

void applyTranscriptCaps(ScoreSet *s, const AnalysisRecord &dialog)
{
    if (!s)
        return;
    QString sellerText;
    int questions = 0;
    bool first = true;
    bool payFirst = false;
    for (const DialogTurn &t : dialog.transcript) {
        if (t.speaker != QLatin1String("seller"))
            continue;
        sellerText += t.text;
        sellerText += QLatin1Char('\n');
        questions += t.text.count(QLatin1Char('?'));
        if (first) {
            first = false;
            const QString low = t.text.toLower();
            if (low.contains(QStringLiteral("оплат")) || low.contains(QStringLiteral("купит"))
                || low.contains(QStringLiteral("подпис")))
                payFirst = true;
        }
    }
    const QString low = sellerText.toLower();
    const bool discount = QRegularExpression(
                              QStringLiteral("скидк\\w*.{0,16}\\d{2}\\s*%"),
                              QRegularExpression::CaseInsensitiveOption)
                              .match(low)
                              .hasMatch();
    const bool best = low.contains(QStringLiteral("лучшее на рынке"));
    const bool buyPush = low.contains(QStringLiteral("предлагаю купить"))
        || low.contains(QStringLiteral("предлагаем купить"))
        || low.contains(QStringLiteral("давите на оплату"));
    const bool wrongCurrency = AppPaths::expectsRubles(dialog.sellerPrompt)
        && AppPaths::hasForeignCurrency(sellerText);
    const int extraGreet = extraSellerGreetings(dialog.transcript);

    if (extraGreet >= 1)
        capScore(&s->contact, 5);
    if (payFirst || questions == 0 || buyPush)
        capScore(&s->needs, 4);
    if (discount || best || wrongCurrency)
        capScore(&s->objections, 4);
    if (discount || payFirst || buyPush || wrongCurrency)
        capScore(&s->offer, 4);
    if (extraGreet >= 1 || payFirst || buyPush)
        capScore(&s->buyerFit, 5);

    if (weakDemoHits(dialog.sellerPromptTemplate) >= 2) {
        capScore(&s->contact, 6);
        capScore(&s->needs, 5);
        capScore(&s->objections, 6);
        capScore(&s->offer, 6);
        capScore(&s->buyerFit, 5);
    }
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
        "mistakes и recommendations — по 5 коротких фраз на русском, строго в порядке критериев. "
        "У каждого критерия своя ошибка и своя рекомендация, не копируй одну фразу на все строки. "
        "Пиши только про продавца: что он сделал не так и что ему делать в следующем диалоге. "
        "Не пиши, что должен сделать покупатель. Не смешивай английские слова. "
        "8–10 только если продавец не давит на оплату с первой реплики, не выдумывает скидки "
        "и не повторяет приветствие. Типичный торопливый скрипт "
        "(купить сразу, скидка 30–50%, «лучшее на рынке», выдуманные цифры) — оценки 3–5.\n"
        "Если по критерию оценка 8–10, в mistakes и recommendations поставь пустые строки. "
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
    applyTranscriptCaps(&r.scores, dialog);
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
        const QString mistake = r.scores.mistakeAt(i);
        if (mistake.isEmpty() && i < flatMistakes.size())
            r.scores.setMistakeAt(i, flatMistakes.at(i));
        const QString recommendation = r.scores.recommendationAt(i);
        if (recommendation.isEmpty() && i < flatRecs.size())
            r.scores.setRecommendationAt(i, flatRecs.at(i));
    }
    auto copiesOf = [](const ScoreSet &s, int i, bool recs) {
        const QString a = (recs ? s.recommendationAt(i) : s.mistakeAt(i)).trimmed();
        int n = 0;
        for (int j = 0; j < 5; ++j) {
            const QString b = (recs ? s.recommendationAt(j) : s.mistakeAt(j)).trimmed();
            if (QString::compare(a, b, Qt::CaseInsensitive) == 0)
                ++n;
        }
        return n;
    };
    bool dupErr[5] = {};
    bool dupRec[5] = {};
    for (int i = 0; i < 5; ++i) {
        dupErr[i] = copiesOf(r.scores, i, false) > 1;
        dupRec[i] = copiesOf(r.scores, i, true) > 1;
    }
    for (int i = 0; i < 5; ++i) {
        const QString err = r.scores.mistakeAt(i).trimmed();
        if (err.isEmpty() || looksBuyerFacing(err) || dupErr[i]) {
            r.scores.setMistakeAt(i, fallbackMistakeFor(i, r.scores.valueAt(i)));
        }
        const QString rec = r.scores.recommendationAt(i).trimmed();
        if (rec.isEmpty() || looksBuyerFacing(rec) || dupRec[i]) {
            r.scores.setRecommendationAt(i, fallbackRecFor(i, r.scores.valueAt(i)));
        }
        if (r.scores.valueAt(i) >= 8) {
            r.scores.setMistakeAt(i, {});
            r.scores.setRecommendationAt(i, {});
        }
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
        const QString mistake = r.scores.mistakeAt(i);
        if (!mistake.isEmpty())
            r.mistakes << mistake;
        const QString recommendation = r.scores.recommendationAt(i);
        if (!recommendation.isEmpty())
            r.recommendations << recommendation;
    }
    if (r.newPrompt.isEmpty() || !Catalogs::hasBuyerPlaceholder(r.newPrompt)) {
        r.newPrompt = buildNextSellerPrompt(
            dialog.sellerPromptTemplate, r.scores, r.recommendations);
    } else {
        r.newPrompt = buildNextSellerPrompt(r.newPrompt, r.scores, r.recommendations);
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
    m_baseStatus = QStringLiteral("Анализ диалога #%1…").arg(dialog.dialogId);
    emit progress(m_baseStatus);

    auto kick = [this]() {
        if (!m_busy)
            return;
        if (m_pending.dialogModel.isEmpty()) {
            m_pending.dialogModel = m_llm->dialogModelName().isEmpty()
                ? AppSettings::instance().dialogModel()
                : m_llm->dialogModelName();
        }
        m_pending.analyzerModel = m_llm->analyzerModelName().isEmpty()
            ? AppSettings::instance().analyzerModel()
            : m_llm->analyzerModelName();
        m_baseStatus = QStringLiteral("Анализ диалога #%1 моделью %2 (%3)…")
                           .arg(m_pending.dialogId)
                           .arg(m_pending.analyzerModel, m_llm->analyzerDeviceLabel());
        startWaitClock();
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
