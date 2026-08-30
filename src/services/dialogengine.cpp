#include "dialogengine.h"

#include "core/apppaths.h"
#include "core/appsettings.h"
#include "core/catalogs.h"
#include "localllm.h"

#include <QTimer>

#include <QRegularExpression>

namespace {

QString stripLeadingGreeting(QString text)
{
    static const QRegularExpression re(
        QStringLiteral(
            "^(?:здравствуйте|здравстуйте|добрый\\s+(?:день|вечер|утро)|"
            "доброго\\s+(?:дня|вечера|утра)|приветствую|привет)[\\s!,.…]*"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    for (int i = 0; i < 2; ++i) {
        const QRegularExpressionMatch m = re.match(text);
        if (!m.hasMatch() || m.capturedLength() <= 0)
            break;
        const QString next = text.mid(m.capturedLength()).trimmed();
        if (next.isEmpty())
            break;
        text = next;
    }
    return text;
}

QString fallbackTurn(bool seller)
{
    return seller
        ? QStringLiteral("Давайте уточним детали. Что для вас сейчас важнее всего?")
        : QStringLiteral("Хорошо. А что входит в стоимость и от чего она зависит?");
}

QString stripMetaLeak(QString text)
{
    static const QRegularExpression meta(
        QStringLiteral(
            "(?i)(?:служебно\\s*:|language\\s*lock\\s*:|продолжим\\s+на\\s+русском\\.?|"
            "китайский\\s+и\\s+иероглифы\\s+запрещены\\.?|"
            "ответьте\\s+только\\s+по-русски[^\\n]*)"),
        QRegularExpression::UseUnicodePropertiesOption);
    text.replace(meta, QString());
    return text.simplified();
}

bool speakerAlreadyTalked(const QVector<DialogTurn> &turns, const QString &speaker)
{
    for (const DialogTurn &t : turns) {
        if (t.speaker == speaker)
            return true;
    }
    return false;
}

} // namespace

DialogEngine::DialogEngine(LocalLlm *llm, QObject *parent)
    : QObject(parent)
    , m_llm(llm)
{
    connect(m_llm, &LocalLlm::dialogReply, this, &DialogEngine::onDialogReply);
    connect(m_llm, &LocalLlm::dialogFailed, this, &DialogEngine::onDialogFailed);
    connect(m_llm, &LocalLlm::serversReady, this, [this]() {
        if (m_phase == Phase::WaitServers)
            beginWhenReady();
    });
    connect(m_llm, &LocalLlm::serverFailed, this, [this](const QString &msg) {
        if (!m_busy)
            return;
        stopTurnClock();
        m_busy = false;
        m_phase = Phase::Idle;
        emit failed(msg);
    });
    m_turnTick = new QTimer(this);
    m_turnTick->setInterval(1000);
    connect(m_turnTick, &QTimer::timeout, this, &DialogEngine::emitTurnWait);
}

void DialogEngine::abort()
{
    if (!m_busy)
        return;
    stopTurnClock();
    m_busy = false;
    m_phase = Phase::Idle;
    m_llm->abortPendingGeneration();
    emit aborted();
}

AnalysisRecord DialogEngine::finishEarly()
{
    if (!m_busy)
        return {};
    m_stopping = true;
    stopTurnClock();
    m_llm->abortPendingGeneration();
    recountPairs();
    m_record.elapsedMs = m_timer.elapsed();
    m_busy = false;
    m_phase = Phase::Idle;
    m_stopping = false;
    return m_record;
}

void DialogEngine::recountPairs()
{
    int sellers = 0;
    int buyers = 0;
    for (const DialogTurn &t : m_record.transcript) {
        if (t.speaker == QStringLiteral("seller"))
            ++sellers;
        else if (t.speaker == QStringLiteral("buyer"))
            ++buyers;
    }
    m_record.pairCount = qMin(sellers, buyers);
}

void DialogEngine::startTurnClock(int pair, int target, const QString &base)
{
    m_progPair = pair;
    m_progTarget = target;
    m_baseProgress = base;
    const auto &cfg = AppSettings::instance();
    int ngl = cfg.dialogGpuLayers();
    if (m_llm->dialogServer() && m_llm->dialogServer()->gpuLayers() >= 0)
        ngl = m_llm->dialogServer()->gpuLayers();
    const QString model = m_llm->dialogModelName().isEmpty() ? cfg.dialogModel()
                                                            : m_llm->dialogModelName();
    m_turnEstimateSec = AppPaths::estimateGenerationSec(model, ngl, cfg.dialogMaxTokens());
    m_turnClock.restart();
    if (m_turnTick)
        m_turnTick->start();
    emitTurnWait();
}

void DialogEngine::stopTurnClock()
{
    if (m_turnTick)
        m_turnTick->stop();
}

void DialogEngine::emitTurnWait()
{
    if (!m_busy)
        return;
    emit progress(m_progPair, m_progTarget,
                  m_baseProgress + QLatin1Char(' ')
                      + AppPaths::formatWaitHint(m_turnClock.elapsed(), m_turnEstimateSec));
}

void DialogEngine::start(const CatalogItem &product,
                         const CatalogItem &buyer,
                         const QString &sellerPrompt,
                         const QString &buyerPrompt,
                         const QString &seriesId,
                         int cycleIndex,
                         int targetPairs)
{
    if (m_busy)
        return;

    m_record = AnalysisRecord();
    m_record.productItem = product.item;
    m_record.productDescr = product.descr;
    m_record.buyerType = buyer.item;
    m_record.buyerDescr = buyer.descr;
    m_record.sellerPromptTemplate = sellerPrompt;
    m_record.sellerPrompt = Catalogs::substitutePlaceholders(sellerPrompt, product, buyer);
    m_record.seriesId = seriesId;
    m_record.cycleIndex = cycleIndex;
    const QString extraRules = QStringLiteral(
        "\n\nLANGUAGE LOCK: отвечайте только на русском языке, кириллицей. "
        "Запрещены китайский, японский, корейский, иероглифы, латиница целыми фразами. "
        "Если мысль на другом языке — сразу перескажите её по-русски. "
        "Никогда не продолжайте чужой ответ на китайском.\n"
        "Поприветствуйте собеседника только в самой первой своей реплике. "
        "Дальше не начинайте с «Здравствуйте», «Добрый день», «Привет».");
    m_langRetries = 0;
    m_sellerSystem = m_record.sellerPrompt + extraRules;
    m_buyerSystem = Catalogs::substitutePlaceholders(buyerPrompt, product, buyer) + extraRules;
    m_targetPairs = (targetPairs >= 5) ? qBound(5, targetPairs, 20)
                                       : AppSettings::instance().targetPairs();
    m_busy = true;
    m_phase = Phase::WaitServers;
    m_timer.restart();

    startTurnClock(0, m_targetPairs,
                   QStringLiteral("Ожидание llama-server (%1, %2)…")
                       .arg(m_llm->dialogModelName().isEmpty()
                                ? AppSettings::instance().dialogModel()
                                : m_llm->dialogModelName(),
                            m_llm->dialogDeviceLabel()));

    if (m_llm->isDialogReady())
        beginWhenReady();
    else
        m_llm->preloadFromSettings();
}

void DialogEngine::beginWhenReady()
{
    if (!m_busy || m_phase != Phase::WaitServers)
        return;
    requestSeller();
}

void DialogEngine::requestSeller()
{
    m_phase = Phase::Seller;
    const int nextPair = m_record.pairCount + 1;
    startTurnClock(nextPair, m_targetPairs,
                   QStringLiteral("Реплика %1/%2… продавец (%3, %4)")
                       .arg(nextPair)
                       .arg(m_targetPairs)
                       .arg(m_llm->dialogModelName(), m_llm->dialogDeviceLabel()));
    m_llm->generateDialog(m_sellerSystem, withLanguageLock(sellerHistory(), m_langRetries > 0));
}

void DialogEngine::requestBuyer()
{
    m_phase = Phase::Buyer;
    startTurnClock(m_record.pairCount + 1, m_targetPairs,
                   QStringLiteral("Реплика %1/%2… покупатель (%3, %4)")
                       .arg(m_record.pairCount + 1)
                       .arg(m_targetPairs)
                       .arg(m_llm->dialogModelName(), m_llm->dialogDeviceLabel()));
    m_llm->generateDialog(m_buyerSystem, withLanguageLock(buyerHistory(), m_langRetries > 0));
}

QVector<ChatMessage> DialogEngine::sellerHistory() const
{
    QVector<ChatMessage> hist;
    if (m_record.transcript.isEmpty()) {
        hist.push_back({QStringLiteral("user"),
                        QStringLiteral("Клиент только что подключился. "
                                       "Поприветствуйте один раз и сразу переходите к делу.")});
        return hist;
    }
    for (const DialogTurn &t : m_record.transcript) {
        if (t.speaker == QStringLiteral("seller"))
            hist.push_back({QStringLiteral("assistant"), t.text});
        else
            hist.push_back({QStringLiteral("user"), t.text});
    }
    return hist;
}

QVector<ChatMessage> DialogEngine::buyerHistory() const
{
    QVector<ChatMessage> hist;
    for (const DialogTurn &t : m_record.transcript) {
        if (t.speaker == QStringLiteral("buyer"))
            hist.push_back({QStringLiteral("assistant"), t.text});
        else
            hist.push_back({QStringLiteral("user"), t.text});
    }
    if (hist.isEmpty())
        hist.push_back({QStringLiteral("user"),
                        QStringLiteral("Продавец ещё не успел заговорить. "
                                       "Коротко скажите, зачем вы здесь, без лишних приветствий.")});
    return hist;
}

QVector<ChatMessage> DialogEngine::withLanguageLock(QVector<ChatMessage> hist, bool retry)
{
    if (!retry)
        return hist;
    const QString lock = QStringLiteral(
        "Повторите предыдущую мысль только по-русски, без иероглифов.");
    if (hist.isEmpty() || hist.last().role != QStringLiteral("user"))
        hist.push_back({QStringLiteral("user"), lock});
    else
        hist.last().content += QLatin1Char('\n') + lock;
    return hist;
}

void DialogEngine::onDialogReply(const QString &text)
{
    if (m_stopping || !m_busy)
        return;
    if (m_phase != Phase::Seller && m_phase != Phase::Buyer)
        return;

    if (AppPaths::hasCjk(text) && m_langRetries < 1) {
        ++m_langRetries;
        if (m_phase == Phase::Seller)
            requestSeller();
        else
            requestBuyer();
        return;
    }

    const bool seller = (m_phase == Phase::Seller);
    QString clean = stripMetaLeak(AppPaths::stripCjk(text.trimmed()));
    if (clean.size() < 8)
        clean = fallbackTurn(seller);
    m_langRetries = 0;

    DialogTurn turn;
    turn.speaker = seller ? QStringLiteral("seller") : QStringLiteral("buyer");
    if (speakerAlreadyTalked(m_record.transcript, turn.speaker))
        clean = stripLeadingGreeting(clean);
    turn.text = clean;
    m_record.transcript.push_back(turn);
    emit turnReady(turn);

    if (m_phase == Phase::Seller) {
        requestBuyer();
        return;
    }

    m_record.pairCount += 1;
    if (m_record.pairCount >= m_targetPairs) {
        completeOk();
        return;
    }
    requestSeller();
}

void DialogEngine::onDialogFailed(const QString &error)
{
    if (m_stopping || !m_busy)
        return;
    if (m_record.pairCount >= 5) {
        completeOk();
        return;
    }
    stopTurnClock();
    m_busy = false;
    m_phase = Phase::Idle;
    emit failed(error);
}

void DialogEngine::completeOk()
{
    stopTurnClock();
    m_record.elapsedMs = m_timer.elapsed();
    m_busy = false;
    m_phase = Phase::Idle;
    emit finished(m_record);
}
