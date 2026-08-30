#include "apppaths.h"

#include <QChar>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

QString AppPaths::appDir()
{
    return QCoreApplication::applicationDirPath();
}

QStringList AppPaths::dataSearchRoots()
{
    QStringList roots;
    const QString app = appDir();
    auto add = [&roots](const QString &p) {
        const QString n = QDir(p).absolutePath();
        if (!n.isEmpty() && !roots.contains(n))
            roots << n;
    };
    add(QDir(app).filePath(QStringLiteral("data")));
    add(QDir(app).filePath(QStringLiteral("../data")));
    add(QDir(app).filePath(QStringLiteral("../../data")));
    add(QDir(app).filePath(QStringLiteral("../../../data")));
#ifdef SALER_SOURCE_DIR
    add(QDir(QString::fromUtf8(SALER_SOURCE_DIR)).filePath(QStringLiteral("data")));
#endif
    add(QDir::current().filePath(QStringLiteral("data")));
    return roots;
}

QString AppPaths::findDataFile(const QString &fileName)
{
    for (const QString &root : dataSearchRoots()) {
        const QString path = QDir(root).filePath(fileName);
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QString AppPaths::writableDataDir()
{
    for (const QString &root : dataSearchRoots()) {
        QDir dir(root);
        if (dir.exists()) {
            QFile probe(dir.filePath(QStringLiteral(".write_probe")));
            if (probe.open(QIODevice::WriteOnly)) {
                probe.remove();
                return dir.absolutePath();
            }
        }
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    QDir dataDir(QDir(appData).filePath(QStringLiteral("data")));
    dataDir.mkpath(QStringLiteral("."));
    return dataDir.absolutePath();
}

QString AppPaths::resultsFilePath()
{
    return QDir(writableDataDir()).filePath(QStringLiteral("results.json"));
}

QString AppPaths::llamaServerPath(const QString &binDir)
{
#ifdef Q_OS_WIN
    const QStringList names = {
        QStringLiteral("llama-server.exe"),
        QStringLiteral("llama.exe"),
    };
#else
    const QStringList names = {
        QStringLiteral("llama-server"),
        QStringLiteral("llama"),
    };
#endif
    for (const QString &name : names) {
        const QString path = QDir(binDir).filePath(name);
        if (QFileInfo::exists(path))
            return path;
    }
    return QDir(binDir).filePath(names.first());
}

QString AppPaths::llamaCliPath(const QString &binDir)
{
#ifdef Q_OS_WIN
    return QDir(binDir).filePath(QStringLiteral("llama-cli.exe"));
#else
    return QDir(binDir).filePath(QStringLiteral("llama-cli"));
#endif
}

QVector<GgufModelInfo> AppPaths::discoverGgufModels(const QString &modelsDir)
{
    QVector<GgufModelInfo> result;
    QDir dir(modelsDir);
    if (!dir.exists())
        return result;

    const QFileInfoList files = dir.entryInfoList(QStringList{QStringLiteral("*.gguf")}, QDir::Files);
    for (const QFileInfo &fi : files) {
        GgufModelInfo info;
        info.fileName = fi.fileName();
        info.absolutePath = fi.absoluteFilePath();
        info.sizeBytes = fi.size();
        result.push_back(info);
    }
    std::sort(result.begin(), result.end(), [](const GgufModelInfo &a, const GgufModelInfo &b) {
        if (a.sizeBytes != b.sizeBytes)
            return a.sizeBytes > b.sizeBytes;
        return a.fileName.compare(b.fileName, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QString AppPaths::formatDiskSize(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("0 B");
    const double kb = 1024.0;
    if (bytes < kb * kb)
        return QStringLiteral("%1 КБ").arg(qRound(bytes / kb));
    if (bytes < kb * kb * kb)
        return QStringLiteral("%1 МБ").arg(bytes / (kb * kb), 0, 'f', 1);
    return QStringLiteral("%1 ГБ").arg(bytes / (kb * kb * kb), 0, 'f', 2);
}

int AppPaths::guessMaxLayers(const QString &fileName)
{
    const QString lower = fileName.toLower();
    if (lower.contains(QStringLiteral("1.5b")) || lower.contains(QStringLiteral("1_5b")))
        return 28;
    if (lower.contains(QStringLiteral("3b")))
        return 36;
    if (lower.contains(QStringLiteral("qwen"))
        && (lower.contains(QStringLiteral("7b")) || lower.contains(QStringLiteral("8b"))))
        return 28;
    if (lower.contains(QStringLiteral("7b")) || lower.contains(QStringLiteral("8b")))
        return 32;
    return 32;
}

QString AppPaths::computeDeviceLabel(const QString &fileName, int gpuLayers)
{
    if (gpuLayers <= 0)
        return QStringLiteral("CPU");
    const int maxLayers = qMax(1, guessMaxLayers(fileName));
    if (gpuLayers >= 99 || gpuLayers >= maxLayers)
        return QStringLiteral("GPU");
    return QStringLiteral("both");
}

QString AppPaths::gpuOffloadHint(const QString &fileName, int gpuLayers)
{
    if (fileName.isEmpty())
        return {};
    const int maxLayers = qMax(1, guessMaxLayers(fileName));
    const QString device = computeDeviceLabel(fileName, gpuLayers);
    if (device == QStringLiteral("CPU")) {
        return QStringLiteral("%1: CPU (0 слоёв на GPU). Чтобы модель целиком на видеокарте — "
                              "поставьте %2 или 99.")
            .arg(fileName)
            .arg(maxLayers);
    }
    if (device == QStringLiteral("GPU")) {
        return QStringLiteral("%1: GPU — все %2 слоёв на видеокарте.")
            .arg(fileName)
            .arg(maxLayers);
    }
    return QStringLiteral("%1: both — на GPU %2 из %3 слоёв, остальные считает CPU. "
                          "Чтобы только GPU, поставьте %3 или 99.")
        .arg(fileName)
        .arg(gpuLayers)
        .arg(maxLayers);
}

int AppPaths::estimateGenerationSec(const QString &fileName, int gpuLayers, int maxTokens)
{
    const double paramsB = qMax(1.0, guessParamBillions(fileName));
    const QString device = computeDeviceLabel(fileName, gpuLayers);

    double tokensPerSec = 5.0;
    int promptEval = 8;
    int minSec = 12;
    if (device == QStringLiteral("GPU")) {
        if (paramsB <= 2.0)
            tokensPerSec = 90.0;
        else if (paramsB <= 4.0)
            tokensPerSec = 80.0;
        else
            tokensPerSec = 45.0;
        promptEval = 2 + int(paramsB * 0.4);
        minSec = 4;
    } else if (device == QStringLiteral("both")) {
        if (paramsB <= 2.0)
            tokensPerSec = 22.0;
        else if (paramsB <= 4.0)
            tokensPerSec = 14.0;
        else
            tokensPerSec = 7.0;
        promptEval = 5 + int(paramsB * 1.5);
        minSec = 8;
    } else if (paramsB <= 2.0) {
        tokensPerSec = 20.0;
        promptEval = 6 + int(paramsB * 2.5);
    } else if (paramsB <= 4.0) {
        tokensPerSec = 12.0;
        promptEval = 6 + int(paramsB * 2.5);
    } else {
        tokensPerSec = 4.5;
        promptEval = 6 + int(paramsB * 2.5);
    }

    const int n = qMax(32, maxTokens);
    return qBound(minSec, int(n / tokensPerSec) + promptEval, 20 * 60);
}

QString AppPaths::formatElapsed(qint64 elapsedMs)
{
    const int s = qMax(0, int(elapsedMs / 1000));
    if (s < 60)
        return QStringLiteral("%1 с").arg(s);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString AppPaths::formatWaitHint(qint64 elapsedMs, int estimateSec)
{
    return QStringLiteral("прошло %1, ориентир ~%2")
        .arg(formatElapsed(elapsedMs), formatElapsed(qint64(estimateSec) * 1000));
}

bool AppPaths::isCjkCodepoint(uint u)
{
    return (u >= 0x2E80 && u <= 0x2EFF)
        || (u >= 0x2F00 && u <= 0x2FDF)
        || (u >= 0x3000 && u <= 0x303F)
        || (u >= 0x3040 && u <= 0x30FF)
        || (u >= 0x31C0 && u <= 0x31EF)
        || (u >= 0x31F0 && u <= 0x31FF)
        || (u >= 0x3400 && u <= 0x4DBF)
        || (u >= 0x4E00 && u <= 0x9FFF)
        || (u >= 0xF900 && u <= 0xFAFF)
        || (u >= 0xFF00 && u <= 0xFFEF)
        || (u >= 0xAC00 && u <= 0xD7AF)
        || (u >= 0x20000 && u <= 0x2FA1F);
}

bool AppPaths::hasCjk(const QString &text)
{
    for (int i = 0; i < text.size();) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            if (isCjkCodepoint(QChar::surrogateToUcs4(c, text.at(i + 1))))
                return true;
            i += 2;
            continue;
        }
        if (isCjkCodepoint(c.unicode()))
            return true;
        ++i;
    }
    return false;
}

QString AppPaths::stripCjk(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size();) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            const uint u = QChar::surrogateToUcs4(c, text.at(i + 1));
            if (!isCjkCodepoint(u)) {
                out += c;
                out += text.at(i + 1);
            }
            i += 2;
            continue;
        }
        if (!isCjkCodepoint(c.unicode()))
            out += c;
        ++i;
    }
    return out.simplified();
}

double AppPaths::guessParamBillions(const QString &fileName)
{
    const QRegularExpression re(QStringLiteral(R"((\d+(?:\.\d+)?)[_-]?b)"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(fileName);
    if (m.hasMatch())
        return m.captured(1).toDouble();
    return 3.0;
}

double AppPaths::estimateGpuMb(const QString &fileName, qint64 fileBytes,
                               int gpuLayers, int contextTokens)
{
    if (gpuLayers <= 0 || fileBytes <= 0)
        return 0.0;

    const int maxLayers = qMax(1, guessMaxLayers(fileName));
    const double frac = qBound(0.05, gpuLayers / double(maxLayers), 1.0);
    const double weightsMb = (fileBytes / (1024.0 * 1024.0)) * frac;
    const double paramsB = guessParamBillions(fileName);
    const double kvMb = paramsB * (qMax(512, contextTokens) / 1024.0) * 2.2 * frac;
    const double overheadMb = 384.0 + 48.0 * frac;
    return weightsMb + kvMb + overheadMb;
}

QString AppPaths::vramWarning(const QString &fileName, qint64 fileBytes,
                              int gpuLayers, int contextTokens, double budgetMb)
{
    const double est = estimateGpuMb(fileName, fileBytes, gpuLayers, contextTokens);
    if (est <= 0.0 || est < budgetMb)
        return {};
    return QStringLiteral(
               "Внимание: модель %1 при %2 слоях GPU может не влезть в ~4 ГБ VRAM "
               "(оценка ~%3 МБ, запас %4 МБ). Уменьшите слои GPU или выберите модель легче.")
        .arg(fileName)
        .arg(gpuLayers)
        .arg(est, 0, 'f', 0)
        .arg(budgetMb, 0, 'f', 0);
}
