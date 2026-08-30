#include "localllm.h"

#include "core/apppaths.h"
#include "core/appsettings.h"
#include "core/childcleanup.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>

namespace {

QJsonArray historyToMessages(const QString &systemPrompt, const QVector<ChatMessage> &history)
{
    QJsonArray messages;
    QJsonObject sys;
    sys.insert(QStringLiteral("role"), QStringLiteral("system"));
    sys.insert(QStringLiteral("content"), systemPrompt);
    messages.append(sys);
    for (const ChatMessage &m : history) {
        QJsonObject o;
        o.insert(QStringLiteral("role"), m.role);
        o.insert(QStringLiteral("content"), m.content);
        messages.append(o);
    }
    return messages;
}

QString buildCompletionPrompt(const QString &systemPrompt, const QVector<ChatMessage> &history)
{
    QString prompt = QStringLiteral("### System:\n") + systemPrompt + QStringLiteral("\n\n");
    for (const ChatMessage &m : history) {
        const QString tag = m.role == QStringLiteral("assistant")
            ? QStringLiteral("Assistant")
            : QStringLiteral("User");
        prompt += QStringLiteral("### ") + tag + QStringLiteral(":\n") + m.content + QStringLiteral("\n\n");
    }
    prompt += QStringLiteral("### Assistant:\n");
    return prompt;
}

bool looksLikeOom(const QString &text)
{
    const QString lower = text.toLower();
    return lower.contains(QStringLiteral("out of memory"))
        || lower.contains(QStringLiteral("failed to allocate"))
        || lower.contains(QStringLiteral("oom"))
        || lower.contains(QStringLiteral("vk::outofdevice"))
        || lower.contains(QStringLiteral("outofdevicememory"))
        || lower.contains(QStringLiteral("cuda_error_out_of_memory"))
        || lower.contains(QStringLiteral("ggml_gallocr"))
        || lower.contains(QStringLiteral("failed to load model"));
}

} // namespace

LlamaServer::LlamaServer(const QString &roleLabel, QObject *parent)
    : QObject(parent)
    , m_roleLabel(roleLabel)
{
    m_nam = new QNetworkAccessManager(this);
    m_healthTimer = new QTimer(this);
    m_healthTimer->setInterval(600);
    connect(m_healthTimer, &QTimer::timeout, this, &LlamaServer::pollHealth);
}

LlamaServer::~LlamaServer()
{
    stop();
}

bool LlamaServer::sameConfig(const QString &modelPath, int port, int gpuLayers, int context, int threads) const
{
    const QString a = QFileInfo(m_modelPath).canonicalFilePath();
    const QString b = QFileInfo(modelPath).canonicalFilePath();
    const QString left = a.isEmpty() ? QFileInfo(m_modelPath).absoluteFilePath() : a;
    const QString right = b.isEmpty() ? QFileInfo(modelPath).absoluteFilePath() : b;
    return QString::compare(left, right, Qt::CaseInsensitive) == 0
        && m_port == port && m_gpuLayers == gpuLayers && m_context == context && m_threads == threads;
}

bool LlamaServer::matches(const QString &modelPath, int port, int gpuLayers, int context, int threads) const
{
    return m_ready && m_proc && m_proc->state() == QProcess::Running
        && sameConfig(modelPath, port, gpuLayers, context, threads);
}

void LlamaServer::stop()
{
    m_healthTimer->stop();
    if (m_healthReply) {
        m_healthReply->abort();
        m_healthReply->deleteLater();
        m_healthReply = nullptr;
    }
    if (m_genReply) {
        m_genReply->abort();
        m_genReply->deleteLater();
        m_genReply = nullptr;
    }
    const bool wasReady = m_ready;
    m_ready = false;
    m_starting = false;
    if (m_proc) {
        ChildCleanup::detachLlamaChild(m_proc->processId());
        m_proc->disconnect(this);
        m_proc->terminate();
        if (!m_proc->waitForFinished(2500))
            m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    if (wasReady)
        emit readyChanged(false);
}

void LlamaServer::failStart(const QString &msg)
{
    m_error = msg;
    m_starting = false;
    m_ready = false;
    m_healthTimer->stop();
    emit startFailed(m_modelFileName, msg);
}

void LlamaServer::startAsync(const QString &binDir,
                             const QString &modelPath,
                             int port,
                             int gpuLayers,
                             int context,
                             int threads)
{
    if (matches(modelPath, port, gpuLayers, context, threads)) {
        emit readyChanged(true);
        return;
    }
    if (m_starting && m_proc && sameConfig(modelPath, port, gpuLayers, context, threads))
        return;

    stop();

    const QString stale = ChildCleanup::reapStaleLlamaOnPort(port);
    if (!stale.isEmpty())
        emit logLine(stale);

    m_binDir = binDir;
    m_modelPath = modelPath;
    m_modelFileName = QFileInfo(modelPath).fileName();
    m_port = port;
    m_gpuLayers = gpuLayers;
    m_context = context;
    m_threads = threads;
    m_error.clear();
    m_logTail.clear();

    const QString exe = AppPaths::llamaServerPath(binDir);
    if (!QFileInfo::exists(exe)) {
        failStart(QStringLiteral("Не найден llama-server в %1 (модель %2).")
                      .arg(binDir, m_modelFileName));
        return;
    }
    if (!QFileInfo::exists(modelPath)) {
        failStart(QStringLiteral("LLM-модель не найдена: %1").arg(modelPath));
        return;
    }

    m_starting = true;
    m_startDeadline = QDateTime::currentMSecsSinceEpoch() + 300000;

    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(binDir);
    m_proc->setProgram(exe);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), binDir + QDir::listSeparator() + env.value(QStringLiteral("PATH")));
    m_proc->setProcessEnvironment(env);
    m_proc->setArguments({
        QStringLiteral("-m"), modelPath,
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(port),
        QStringLiteral("-ngl"), QString::number(gpuLayers),
        QStringLiteral("-t"), QString::number(threads),
        QStringLiteral("-c"), QString::number(context),
        QStringLiteral("--mmap"),
        QStringLiteral("--no-warmup"),
    });

    connect(m_proc, &QProcess::readyRead, this, [this]() {
        appendLog(m_proc->readAll());
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                onProcessFinished(code);
            });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            failStart(QStringLiteral("Не удалось запустить llama-server для модели %1.").arg(m_modelFileName));
        }
    });

    m_proc->start();
    if (m_proc->waitForStarted(8000))
        ChildCleanup::attachLlamaChild(m_proc->processId());
    m_healthTimer->start();
}

void LlamaServer::appendLog(const QByteArray &chunk)
{
    m_logTail += chunk;
    if (m_logTail.size() > 12000)
        m_logTail = m_logTail.right(8000);
    const QString text = QString::fromUtf8(chunk);
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (!t.isEmpty())
            emit logLine(t);
    }
}

void LlamaServer::onProcessFinished(int exitCode)
{
    m_healthTimer->stop();
    if (m_proc) {
        appendLog(m_proc->readAll());
        ChildCleanup::detachLlamaChild(m_proc->processId());
    }
    const QString log = QString::fromUtf8(m_logTail).right(2000);
    QString msg = QStringLiteral("llama-server завершился (код %1) для модели %2.")
                      .arg(exitCode)
                      .arg(m_modelFileName);
    if (looksLikeOom(log))
        msg = QStringLiteral(
                  "Нехватка VRAM (OOM) при загрузке %1 с %2 слоями GPU. "
                  "Частые причины: второй llama-server, анализатор 7B тоже на GPU, "
                  "или не закрытый процесс после прошлого запуска. "
                  "Оставьте анализатор на CPU (0 слоёв) или одну модель на диалог и анализ.")
                  .arg(m_modelFileName)
                  .arg(m_gpuLayers);
    if (!log.trimmed().isEmpty())
        msg += QStringLiteral("\n") + log.trimmed();

    const bool wasStarting = m_starting;
    m_starting = false;
    m_ready = false;
    if (wasStarting || exitCode != 0)
        failStart(msg);
    else
        emit readyChanged(false);
}

void LlamaServer::pollHealth()
{
    if (m_healthReply)
        return;
    if (QDateTime::currentMSecsSinceEpoch() > m_startDeadline) {
        failStart(QStringLiteral("LLM-сервер не ответил за 5 минут при загрузке модели %1.").arg(m_modelFileName));
        stop();
        return;
    }
    if (m_proc && m_proc->state() != QProcess::Running && m_proc->state() != QProcess::Starting)
        return;

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/health").arg(m_port));
    QNetworkRequest req(url);
    req.setTransferTimeout(2000);
    m_healthReply = m_nam->get(req);
    connect(m_healthReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_healthReply;
        m_healthReply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            m_healthTimer->stop();
            m_starting = false;
            m_ready = true;
            emit readyChanged(true);
        }
    });
}

void LlamaServer::abortGeneration()
{
    if (!m_genReply)
        return;
    QNetworkReply *reply = m_genReply;
    m_genReply = nullptr;
    reply->abort();
    reply->deleteLater();
}

void LlamaServer::generate(const QString &systemPrompt,
                           const QVector<ChatMessage> &history,
                           int maxTokens,
                           double temperature,
                           const QString &grammar)
{
    if (!m_ready) {
        failGeneration(QStringLiteral("Сервер %1 (%2) ещё не готов.")
                           .arg(m_roleLabel, m_modelFileName));
        return;
    }
    if (m_genReply) {
        failGeneration(QStringLiteral("Уже идёт генерация на %1.").arg(m_modelFileName));
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("messages"), historyToMessages(systemPrompt, history));
    payload.insert(QStringLiteral("temperature"), temperature);
    payload.insert(QStringLiteral("max_tokens"), maxTokens);
    payload.insert(QStringLiteral("stream"), false);
    payload.insert(QStringLiteral("cache_prompt"), true);
    m_grammar = grammar;
    if (!grammar.isEmpty())
        payload.insert(QStringLiteral("grammar"), grammar);
    m_lastPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_triedCompletionFallback = false;
    m_triedWithoutGrammar = false;
    postChat(m_lastPayload);
}

void LlamaServer::postChat(const QByteArray &body)
{
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/v1/chat/completions").arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(0);
    m_genReply = m_nam->post(req, body);
    connect(m_genReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_genReply;
        m_genReply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        if (status >= 400 && !m_grammar.isEmpty() && !m_triedWithoutGrammar) {
            m_triedWithoutGrammar = true;
            QJsonObject payload = QJsonDocument::fromJson(m_lastPayload).object();
            payload.remove(QStringLiteral("grammar"));
            m_lastPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
            postChat(m_lastPayload);
            return;
        }
        if (status == 404 && !m_triedCompletionFallback) {
            m_triedCompletionFallback = true;
            QJsonObject payload = QJsonDocument::fromJson(m_lastPayload).object();
            const QJsonArray msgs = payload.value(QStringLiteral("messages")).toArray();
            QString system;
            QVector<ChatMessage> hist;
            for (const QJsonValue &v : msgs) {
                const QJsonObject o = v.toObject();
                const QString role = o.value(QStringLiteral("role")).toString();
                const QString content = o.value(QStringLiteral("content")).toString();
                if (role == QStringLiteral("system"))
                    system = content;
                else
                    hist.push_back({role, content});
            }
            QJsonObject comp;
            comp.insert(QStringLiteral("prompt"), buildCompletionPrompt(system, hist));
            comp.insert(QStringLiteral("n_predict"), payload.value(QStringLiteral("max_tokens")).toInt(256));
            comp.insert(QStringLiteral("temperature"), payload.value(QStringLiteral("temperature")).toDouble(0.7));
            comp.insert(QStringLiteral("stream"), false);
            comp.insert(QStringLiteral("stop"), QJsonArray{
                QStringLiteral("\n### User:"),
                QStringLiteral("\n### Assistant:"),
                QStringLiteral("\n### System:"),
            });
            if (!m_grammar.isEmpty() && !m_triedWithoutGrammar)
                comp.insert(QStringLiteral("grammar"), m_grammar);
            postCompletion(QJsonDocument(comp).toJson(QJsonDocument::Compact));
            return;
        }
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            failGeneration(QStringLiteral("Ошибка запроса к %1 (%2): %3")
                               .arg(m_modelFileName, m_roleLabel, reply->errorString()));
            return;
        }
        if (status >= 400) {
            failGeneration(QStringLiteral("HTTP %1 от llama-server (%2): %3")
                               .arg(status)
                               .arg(m_modelFileName)
                               .arg(QString::fromUtf8(raw.left(400))));
            return;
        }
        QString parseErr;
        const QString content = extractContent(raw, &parseErr);
        if (content.isEmpty()) {
            failGeneration(parseErr.isEmpty()
                               ? QStringLiteral("Пустой ответ модели %1.").arg(m_modelFileName)
                               : parseErr);
            return;
        }
        finishGeneration(content);
    });
}

void LlamaServer::postCompletion(const QByteArray &body)
{
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/completion").arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(0);
    m_genReply = m_nam->post(req, body);
    connect(m_genReply, &QNetworkReply::finished, this, [this, body]() {
        QNetworkReply *reply = m_genReply;
        m_genReply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            failGeneration(QStringLiteral("Ошибка /completion (%1): %2")
                               .arg(m_modelFileName, reply->errorString()));
            return;
        }
        if (status >= 400 && !m_grammar.isEmpty() && !m_triedWithoutGrammar) {
            m_triedWithoutGrammar = true;
            QJsonObject payload = QJsonDocument::fromJson(body).object();
            payload.remove(QStringLiteral("grammar"));
            postCompletion(QJsonDocument(payload).toJson(QJsonDocument::Compact));
            return;
        }
        if (status >= 400) {
            failGeneration(QStringLiteral("HTTP %1 /completion (%2): %3")
                               .arg(status)
                               .arg(m_modelFileName)
                               .arg(QString::fromUtf8(raw.left(400))));
            return;
        }
        QString parseErr;
        const QString content = extractContent(raw, &parseErr);
        if (content.isEmpty()) {
            failGeneration(parseErr.isEmpty()
                               ? QStringLiteral("Пустой ответ модели %1.").arg(m_modelFileName)
                               : parseErr);
            return;
        }
        finishGeneration(content);
    });
}

QString LlamaServer::extractContent(const QByteArray &body, QString *errorOut) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        const QString plain = QString::fromUtf8(body).trimmed();
        if (!plain.isEmpty() && !plain.startsWith(QLatin1Char('{')))
            return plain;
        if (errorOut)
            *errorOut = QStringLiteral("Не-JSON ответ модели %1.").arg(m_modelFileName);
        return {};
    }
    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("error"))) {
        const QJsonValue errVal = obj.value(QStringLiteral("error"));
        const QString msg = errVal.isObject()
            ? errVal.toObject().value(QStringLiteral("message")).toString()
            : errVal.toString();
        if (errorOut)
            *errorOut = QStringLiteral("%1 (%2)").arg(msg, m_modelFileName);
        return {};
    }

    auto pick = [](const QJsonObject &source) -> QString {
        QString content = source.value(QStringLiteral("content")).toString().trimmed();
        if (content.isEmpty() && source.value(QStringLiteral("message")).isObject())
            content = source.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString().trimmed();
        if (content.isEmpty())
            content = source.value(QStringLiteral("completion")).toString().trimmed();
        return content;
    };

    QString content = pick(obj);
    if (content.isEmpty()) {
        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty())
            content = pick(choices.first().toObject());
    }
    return content.trimmed();
}

void LlamaServer::finishGeneration(const QString &text)
{
    emit generationFinished(text.trimmed());
}

void LlamaServer::failGeneration(const QString &msg)
{
    m_error = msg;
    emit generationFailed(msg);
}

LocalLlm::LocalLlm(QObject *parent)
    : QObject(parent)
{
    m_dialog = new LlamaServer(QStringLiteral("диалог"), this);
    m_analyzer = new LlamaServer(QStringLiteral("анализатор"), this);

    connect(m_dialog, &LlamaServer::readyChanged, this, &LocalLlm::onDialogReadyChanged);
    connect(m_analyzer, &LlamaServer::readyChanged, this, &LocalLlm::onAnalyzerReadyChanged);
    connect(m_dialog, &LlamaServer::startFailed, this, [this](const QString &m, const QString &e) {
        onInstanceFailed(m_dialog, m, e);
    });
    connect(m_analyzer, &LlamaServer::startFailed, this, [this](const QString &m, const QString &e) {
        onInstanceFailed(m_analyzer, m, e);
    });
    connect(m_dialog, &LlamaServer::generationFinished, this, [this](const QString &t) {
        onServerText(m_dialog, t);
    });
    connect(m_dialog, &LlamaServer::generationFailed, this, [this](const QString &e) {
        onServerGenFail(m_dialog, e);
    });
    connect(m_analyzer, &LlamaServer::generationFinished, this, [this](const QString &t) {
        onServerText(m_analyzer, t);
    });
    connect(m_analyzer, &LlamaServer::generationFailed, this, [this](const QString &e) {
        onServerGenFail(m_analyzer, e);
    });
}

LocalLlm::~LocalLlm()
{
    shutdown();
}

bool LocalLlm::usesSharedServer() const
{
    return m_shared;
}

bool LocalLlm::isDialogReady() const
{
    return m_dialog && m_dialog->isReady();
}

bool LocalLlm::isAnalyzerReady() const
{
    if (m_shared)
        return isDialogReady();
    return m_analyzer && m_analyzer->isReady();
}

bool LocalLlm::bothReady() const
{
    return isDialogReady() && isAnalyzerReady();
}

QString LocalLlm::dialogModelName() const
{
    return m_dialog ? m_dialog->modelFileName() : QString();
}

QString LocalLlm::analyzerModelName() const
{
    if (m_shared)
        return dialogModelName();
    return m_analyzer ? m_analyzer->modelFileName() : QString();
}

QString LocalLlm::dialogDeviceLabel() const
{
    const auto &cfg = AppSettings::instance();
    const int ngl = (m_dialog && m_dialog->gpuLayers() >= 0)
        ? m_dialog->gpuLayers()
        : cfg.dialogGpuLayers();
    const QString name = dialogModelName().isEmpty() ? cfg.dialogModel() : dialogModelName();
    return AppPaths::computeDeviceLabel(name, ngl);
}

QString LocalLlm::analyzerDeviceLabel() const
{
    if (m_shared)
        return dialogDeviceLabel();
    const auto &cfg = AppSettings::instance();
    const int ngl = (m_analyzer && m_analyzer->gpuLayers() >= 0)
        ? m_analyzer->gpuLayers()
        : cfg.analyzerGpuLayers();
    const QString name = analyzerModelName().isEmpty() ? cfg.analyzerModel() : analyzerModelName();
    return AppPaths::computeDeviceLabel(name, ngl);
}

QStringList LocalLlm::vramWarnings() const
{
    const auto &cfg = AppSettings::instance();
    QStringList warns;
    const auto models = AppPaths::discoverGgufModels(cfg.modelsDir());
    auto find = [&models](const QString &name) -> GgufModelInfo {
        for (const auto &m : models) {
            if (m.fileName.compare(name, Qt::CaseInsensitive) == 0)
                return m;
        }
        return {};
    };
    const GgufModelInfo d = find(cfg.dialogModel());
    const QString w1 = AppPaths::vramWarning(d.fileName, d.sizeBytes, cfg.dialogGpuLayers(), cfg.dialogContext());
    if (!w1.isEmpty())
        warns << w1;
    if (cfg.dialogModel().compare(cfg.analyzerModel(), Qt::CaseInsensitive) != 0) {
        const GgufModelInfo a = find(cfg.analyzerModel());
        const QString w2 = AppPaths::vramWarning(a.fileName, a.sizeBytes, cfg.analyzerGpuLayers(), cfg.analyzerContext());
        if (!w2.isEmpty())
            warns << w2;
    }
    return warns;
}

void LocalLlm::onInstanceFailed(LlamaServer *srv, const QString &model, const QString &error)
{
    Q_UNUSED(srv);
    Q_UNUSED(model);
    emit serverFailed(error);
}

void LocalLlm::onDialogReadyChanged(bool ready)
{
    if (!ready)
        return;
    emit serversReady();
    if (m_shared)
        emit analyzerServerReady();
    emit statusChanged(m_shared
        ? QStringLiteral("Один llama-server: %1 (порт %2, %3)")
              .arg(dialogModelName())
              .arg(m_dialog->port())
              .arg(dialogDeviceLabel())
        : QStringLiteral("Диалог готов: %1 (:%2, %3). Анализатор загрузится перед разбором и заберёт GPU.")
              .arg(dialogModelName())
              .arg(m_dialog->port())
              .arg(dialogDeviceLabel()));
}

void LocalLlm::onAnalyzerReadyChanged(bool ready)
{
    if (!ready || m_shared)
        return;
    emit analyzerServerReady();
    emit statusChanged(QStringLiteral("Анализатор готов: %1 (:%2, %3)")
                           .arg(analyzerModelName())
                           .arg(m_analyzer->port())
                           .arg(analyzerDeviceLabel()));
}

void LocalLlm::startDialogFromSettings()
{
    const auto &cfg = AppSettings::instance();
    const QString dialogPath = QDir(cfg.modelsDir()).filePath(cfg.dialogModel());
    emit statusChanged(QStringLiteral("Запуск llama-server: %1 …").arg(cfg.dialogModel()));
    m_dialog->startAsync(cfg.llamaBinDir(), dialogPath, cfg.dialogPort(),
                         cfg.dialogGpuLayers(), cfg.dialogContext(), cfg.threads());
}

void LocalLlm::startAnalyzerFromSettings()
{
    const auto &cfg = AppSettings::instance();
    const QString analyzerPath = QDir(cfg.modelsDir()).filePath(cfg.analyzerModel());
    emit statusChanged(QStringLiteral("Запуск анализатора: %1 (слои GPU=%2) …")
                           .arg(cfg.analyzerModel())
                           .arg(cfg.analyzerGpuLayers()));
    m_analyzer->startAsync(cfg.llamaBinDir(), analyzerPath, cfg.analyzerPort(),
                           cfg.analyzerGpuLayers(), cfg.analyzerContext(), cfg.threads());
}

void LocalLlm::ensureDialog()
{
    const auto &cfg = AppSettings::instance();
    m_shared = cfg.sameModel();

    const QString stale = ChildCleanup::reapForeignLlamaServers();
    if (!stale.isEmpty())
        emit statusChanged(stale);

    if (!m_shared && m_analyzer && (m_analyzer->isReady() || m_analyzer->isStarting())) {
        emit statusChanged(QStringLiteral("Освобождаю GPU: выгружаю анализатор…"));
        m_analyzer->stop();
    }

    startDialogFromSettings();
    if (m_shared && m_analyzer)
        m_analyzer->stop();
    if (isDialogReady())
        onDialogReadyChanged(true);
}

void LocalLlm::ensureAnalyzer()
{
    const auto &cfg = AppSettings::instance();
    m_shared = cfg.sameModel();

    if (m_shared) {
        ensureDialog();
        return;
    }

    const QString stale = ChildCleanup::reapForeignLlamaServers();
    if (!stale.isEmpty())
        emit statusChanged(stale);

    if (m_dialog && (m_dialog->isReady() || m_dialog->isStarting())) {
        emit statusChanged(QStringLiteral("Освобождаю GPU: выгружаю диалог…"));
        m_dialog->stop();
    }

    startAnalyzerFromSettings();
    if (isAnalyzerReady())
        onAnalyzerReadyChanged(true);
}

void LocalLlm::preloadFromSettings()
{
    ensureDialog();
}

void LocalLlm::shutdown()
{
    if (m_dialog)
        m_dialog->stop();
    if (m_analyzer)
        m_analyzer->stop();
}

void LocalLlm::onServerText(LlamaServer *srv, const QString &text)
{
    Q_UNUSED(srv);
    const PendingKind kind = m_pending;
    m_pending = PendingKind::None;
    if (kind == PendingKind::None)
        return;
    if (kind == PendingKind::Analyzer)
        emit analyzerReply(text);
    else
        emit dialogReply(text);
}

void LocalLlm::onServerGenFail(LlamaServer *srv, const QString &error)
{
    Q_UNUSED(srv);
    const PendingKind kind = m_pending;
    m_pending = PendingKind::None;
    if (kind == PendingKind::None)
        return;
    if (kind == PendingKind::Analyzer)
        emit analyzerFailed(error);
    else
        emit dialogFailed(error);
}

void LocalLlm::abortPendingGeneration()
{
    m_pending = PendingKind::None;
    if (m_dialog)
        m_dialog->abortGeneration();
    if (m_analyzer)
        m_analyzer->abortGeneration();
}

void LocalLlm::generateDialog(const QString &systemPrompt,
                              const QVector<ChatMessage> &history,
                              int maxTokens)
{
    const auto &cfg = AppSettings::instance();
    const int n = maxTokens > 0 ? maxTokens : cfg.dialogMaxTokens();
    static const QString kRussianGrammar = QStringLiteral(
        "root ::= text\n"
        "text ::= char+\n"
        "char ::= [0-9A-Za-zА-Яа-яёЁ \\t\\n.,!?;:()%+/№#@*=<>«»\"'\\[\\]-]");
    m_pending = PendingKind::Dialog;
    m_dialog->generate(systemPrompt, history, n, 0.55, kRussianGrammar);
}

void LocalLlm::generateAnalyzer(const QString &systemPrompt,
                                const QVector<ChatMessage> &history,
                                int maxTokens)
{
    const auto &cfg = AppSettings::instance();
    const int n = maxTokens > 0 ? maxTokens : cfg.analyzerMaxTokens();
    LlamaServer *srv = m_shared ? m_dialog : m_analyzer;
    if (!srv) {
        emit analyzerFailed(QStringLiteral("Анализатор не создан."));
        return;
    }
    m_pending = PendingKind::Analyzer;
    static const QString kAnalyzerJsonGrammar = QStringLiteral(
        "root ::= \"{\" ws scores-kv \",\" ws mistakes-kv \",\" ws recs-kv ws \"}\"\n"
        "scores-kv ::= \"\\\"scores\\\"\" ws \":\" ws scores-obj\n"
        "mistakes-kv ::= \"\\\"mistakes\\\"\" ws \":\" ws str-arr\n"
        "recs-kv ::= \"\\\"recommendations\\\"\" ws \":\" ws str-arr\n"
        "scores-obj ::= \"{\" ws "
        "\"\\\"contact\\\"\" ws \":\" ws score \",\" ws "
        "\"\\\"needs\\\"\" ws \":\" ws score \",\" ws "
        "\"\\\"objections\\\"\" ws \":\" ws score \",\" ws "
        "\"\\\"offer\\\"\" ws \":\" ws score \",\" ws "
        "\"\\\"buyer_fit\\\"\" ws \":\" ws score "
        "ws \"}\"\n"
        "score ::= \"10\" | [1-9]\n"
        "str-arr ::= \"[\" ws (string (ws \",\" ws string)*)? ws \"]\"\n"
        "string ::= \"\\\"\" chars \"\\\"\"\n"
        "chars ::= char*\n"
        "char ::= [0-9A-Za-zА-Яа-яёЁ \\t.,!?;:()%+/№#*=<>«»'\\[\\]\\-]\n"
        "ws ::= [ \\t\\n]*\n");
    srv->generate(systemPrompt, history, n, 0.1, kAnalyzerJsonGrammar);
}
