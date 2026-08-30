#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/types.h"

#include <QMainWindow>
#include <QUuid>

class QTabWidget;
class LocalLlm;
class DialogEngine;
class Analyzer;
class DialogTab;
class AnalysisTab;
class ResultsTab;
class SettingsTab;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void startFreshDialog();
    void stopCurrentRun();
    void startImprovementCycles();
    void launchDialog(const CatalogItem &product,
                      const QString &sellerPrompt,
                      const QString &seriesId,
                      int cycleIndex);
    void onDialogFinished(const AnalysisRecord &record);
    void onAnalysisFinished(const AnalysisRecord &record);
    void setBusy(bool busy, const QString &status);
    void continueCyclesIfNeeded();
    void resetCycleProgress();
    void updateCycleUi();
    QString withCyclePrefix(const QString &status) const;

    LocalLlm *m_llm = nullptr;
    DialogEngine *m_dialogEngine = nullptr;
    Analyzer *m_analyzer = nullptr;

    QTabWidget *m_tabs = nullptr;
    DialogTab *m_dialogTab = nullptr;
    AnalysisTab *m_analysisTab = nullptr;
    ResultsTab *m_resultsTab = nullptr;
    SettingsTab *m_settingsTab = nullptr;

    bool m_busy = false;
    int m_cyclesLeft = 0;
    int m_cycleCurrent = 0;
    int m_cycleTotal = 0;
    QString m_activeSeriesId;
    CatalogItem m_cycleProduct;
    QString m_cyclePrompt;
};

#endif // MAINWINDOW_H
