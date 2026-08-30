#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QString>

class AppSettings : public QObject
{
    Q_OBJECT

public:
    static AppSettings &instance();

    QString modelsDir() const { return m_modelsDir; }
    void setModelsDir(const QString &path);

    QString dialogModel() const { return m_dialogModel; }
    void setDialogModel(const QString &fileName);

    QString analyzerModel() const { return m_analyzerModel; }
    void setAnalyzerModel(const QString &fileName);

    QString llamaBinDir() const { return m_llamaBinDir; }
    void setLlamaBinDir(const QString &path);

    int dialogGpuLayers() const { return m_dialogGpuLayers; }
    void setDialogGpuLayers(int layers);

    int analyzerGpuLayers() const { return m_analyzerGpuLayers; }
    void setAnalyzerGpuLayers(int layers);

    int threads() const { return m_threads; }
    void setThreads(int threads);

    int dialogPort() const { return m_dialogPort; }
    void setDialogPort(int port);

    int analyzerPort() const { return m_analyzerPort; }
    void setAnalyzerPort(int port);

    int dialogContext() const { return m_dialogContext; }
    int analyzerContext() const { return m_analyzerContext; }
    int dialogMaxTokens() const { return m_dialogMaxTokens; }
    int analyzerMaxTokens() const { return m_analyzerMaxTokens; }

    int targetPairs() const { return m_targetPairs; }
    void setTargetPairs(int pairs);

    QString sellerPrompt() const { return m_sellerPrompt; }
    void setSellerPrompt(const QString &text);

    QString buyerPrompt() const { return m_buyerPrompt; }
    void setBuyerPrompt(const QString &text);

    bool sameModel() const;

    void loadDefaultsIfEmpty();
    void sync();

signals:
    void settingsChanged();

private:
    explicit AppSettings(QObject *parent = nullptr);
    Q_DISABLE_COPY(AppSettings)

    QString m_modelsDir;
    QString m_dialogModel;
    QString m_analyzerModel;
    QString m_llamaBinDir;
    int m_dialogGpuLayers = 0;
    int m_analyzerGpuLayers = 0;
    int m_threads = 4;
    int m_dialogPort = 8088;
    int m_analyzerPort = 8089;
    int m_dialogContext = 4096;
    int m_analyzerContext = 4096;
    int m_dialogMaxTokens = 256;
    int m_analyzerMaxTokens = 400;
    int m_targetPairs = 5;
    QString m_sellerPrompt;
    QString m_buyerPrompt;
};

#endif // APPSETTINGS_H
