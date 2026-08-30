#ifndef APPPATHS_H
#define APPPATHS_H

#include "types.h"

#include <QString>
#include <QStringList>
#include <QVector>

class AppPaths
{
public:
    static QString appDir();
    static QStringList dataSearchRoots();
    static QString findDataFile(const QString &fileName);
    static QString writableDataDir();
    static QString resultsFilePath();

    static QString llamaServerPath(const QString &binDir);
    static QString llamaCliPath(const QString &binDir);

    static QVector<GgufModelInfo> discoverGgufModels(const QString &modelsDir);
    static QString formatDiskSize(qint64 bytes);
    static int guessMaxLayers(const QString &fileName);
    static QString computeDeviceLabel(const QString &fileName, int gpuLayers);
    static QString gpuOffloadHint(const QString &fileName, int gpuLayers);
    static int estimateGenerationSec(const QString &fileName, int gpuLayers, int maxTokens);
    static QString formatElapsed(qint64 elapsedMs);
    static QString formatWaitHint(qint64 elapsedMs, int estimateSec);
    static double guessParamBillions(const QString &fileName);

    static double estimateGpuMb(const QString &fileName, qint64 fileBytes,
                                int gpuLayers, int contextTokens);
    static QString vramWarning(const QString &fileName, qint64 fileBytes,
                               int gpuLayers, int contextTokens, double budgetMb = 3600.0);

    static bool isCjkCodepoint(uint u);
    static bool hasCjk(const QString &text);
    static QString stripCjk(const QString &text);
};

#endif // APPPATHS_H
