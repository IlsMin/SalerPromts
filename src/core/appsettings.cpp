#include "appsettings.h"

#include "apppaths.h"
#include "catalogs.h"

#include <QDir>
#include <QSettings>
#include <QThread>

namespace {
constexpr auto kDefaultModels = "C:/Users/IlsMi/QtVoiceRequter/models/llm";
constexpr auto kDefaultLlama = "C:/DEV/llama_cpp.bin";
}

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    QSettings s;
    m_modelsDir = s.value(QStringLiteral("paths/modelsDir"), QString::fromUtf8(kDefaultModels)).toString();
    m_llamaBinDir = s.value(QStringLiteral("paths/llamaBinDir"), QString::fromUtf8(kDefaultLlama)).toString();
    m_dialogModel = s.value(QStringLiteral("llm/dialogModel")).toString();
    m_analyzerModel = s.value(QStringLiteral("llm/analyzerModel")).toString();
    m_dialogGpuLayers = qBound(0, s.value(QStringLiteral("llm/dialogGpuLayers"), 0).toInt(), 99);
    m_analyzerGpuLayers = qBound(0, s.value(QStringLiteral("llm/analyzerGpuLayers"), 99).toInt(), 99);
    if (!s.value(QStringLiteral("llm/gpuExclusiveHandoff"), false).toBool()) {
        s.setValue(QStringLiteral("llm/gpuExclusiveHandoff"), true);
        if (m_analyzerGpuLayers == 0)
            m_analyzerGpuLayers = 99;
        s.setValue(QStringLiteral("llm/analyzerGpuLayers"), m_analyzerGpuLayers);
        s.sync();
    }
    m_threads = qBound(1, s.value(QStringLiteral("llm/threads"),
                                 qMax(2, QThread::idealThreadCount() - 1)).toInt(), 32);
    m_dialogPort = qBound(1024, s.value(QStringLiteral("llm/dialogPort"), 8088).toInt(), 65535);
    m_analyzerPort = qBound(1024, s.value(QStringLiteral("llm/analyzerPort"), 8089).toInt(), 65535);
    m_targetPairs = qBound(5, s.value(QStringLiteral("dialog/targetPairs"), 5).toInt(), 20);
    m_sellerPrompt = s.value(QStringLiteral("prompts/seller")).toString();
    m_buyerPrompt = s.value(QStringLiteral("prompts/buyer")).toString();
    loadDefaultsIfEmpty();
}

void AppSettings::loadDefaultsIfEmpty()
{
    if (m_modelsDir.isEmpty())
        m_modelsDir = QString::fromUtf8(kDefaultModels);
    if (m_llamaBinDir.isEmpty())
        m_llamaBinDir = QString::fromUtf8(kDefaultLlama);
    if (m_sellerPrompt.trimmed().isEmpty())
        m_sellerPrompt = Catalogs::defaultSellerPrompt();
    if (m_buyerPrompt.trimmed().isEmpty())
        m_buyerPrompt = Catalogs::defaultBuyerPrompt();

    const auto models = AppPaths::discoverGgufModels(m_modelsDir);
    auto has = [&models](const QString &name) {
        for (const auto &m : models) {
            if (m.fileName.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };

    if (m_dialogModel.isEmpty() || !has(m_dialogModel)) {
        const QString prefer3b = QStringLiteral("qwen2.5-3b-instruct-q4_k_m.gguf");
        const QString prefer15 = QStringLiteral("qwen2.5-1.5b-instruct-q4_k_m.gguf");
        if (has(prefer3b))
            m_dialogModel = prefer3b;
        else if (has(prefer15))
            m_dialogModel = prefer15;
        else if (!models.isEmpty())
            m_dialogModel = models.first().fileName;
    }

    if (m_analyzerModel.isEmpty() || !has(m_analyzerModel))
        m_analyzerModel = m_dialogModel;
}

void AppSettings::setModelsDir(const QString &path)
{
    m_modelsDir = QDir(path.trimmed()).absolutePath();
}

void AppSettings::setDialogModel(const QString &fileName)
{
    m_dialogModel = fileName;
}

void AppSettings::setAnalyzerModel(const QString &fileName)
{
    m_analyzerModel = fileName;
}

void AppSettings::setLlamaBinDir(const QString &path)
{
    m_llamaBinDir = QDir(path.trimmed()).absolutePath();
}

void AppSettings::setDialogGpuLayers(int layers)
{
    m_dialogGpuLayers = qBound(0, layers, 99);
}

void AppSettings::setAnalyzerGpuLayers(int layers)
{
    m_analyzerGpuLayers = qBound(0, layers, 99);
}

void AppSettings::setThreads(int threads)
{
    m_threads = qBound(1, threads, 32);
}

void AppSettings::setDialogPort(int port)
{
    m_dialogPort = qBound(1024, port, 65535);
}

void AppSettings::setAnalyzerPort(int port)
{
    m_analyzerPort = qBound(1024, port, 65535);
}

void AppSettings::setTargetPairs(int pairs)
{
    m_targetPairs = qBound(5, pairs, 20);
}

void AppSettings::setSellerPrompt(const QString &text)
{
    m_sellerPrompt = text;
}

void AppSettings::setBuyerPrompt(const QString &text)
{
    m_buyerPrompt = text;
}

bool AppSettings::sameModel() const
{
    return QString::compare(m_dialogModel, m_analyzerModel, Qt::CaseInsensitive) == 0;
}

void AppSettings::sync()
{
    QSettings s;
    s.setValue(QStringLiteral("paths/modelsDir"), m_modelsDir);
    s.setValue(QStringLiteral("paths/llamaBinDir"), m_llamaBinDir);
    s.setValue(QStringLiteral("llm/dialogModel"), m_dialogModel);
    s.setValue(QStringLiteral("llm/analyzerModel"), m_analyzerModel);
    s.setValue(QStringLiteral("llm/dialogGpuLayers"), m_dialogGpuLayers);
    s.setValue(QStringLiteral("llm/analyzerGpuLayers"), m_analyzerGpuLayers);
    s.setValue(QStringLiteral("llm/threads"), m_threads);
    s.setValue(QStringLiteral("llm/dialogPort"), m_dialogPort);
    s.setValue(QStringLiteral("llm/analyzerPort"), m_analyzerPort);
    s.setValue(QStringLiteral("dialog/targetPairs"), m_targetPairs);
    s.setValue(QStringLiteral("prompts/seller"), m_sellerPrompt);
    s.setValue(QStringLiteral("prompts/buyer"), m_buyerPrompt);
    s.sync();
    emit settingsChanged();
}
