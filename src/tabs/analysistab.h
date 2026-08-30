#ifndef ANALYSISTAB_H
#define ANALYSISTAB_H

#include "core/types.h"

#include <QWidget>

class QComboBox;
class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QLabel;

class AnalysisTab : public QWidget
{
    Q_OBJECT

public:
    explicit AnalysisTab(QWidget *parent = nullptr);

    void reloadFromStore();
    void reloadStarterPrompt();
    void addRecord(const AnalysisRecord &record, bool seriesStart = false);
    void selectDialogId(int dialogId);
    AnalysisRecord selectedRecord() const;
    QString currentPrompt() const;
    int cycleCount() const;
    void setBusy(bool busy);
    void setCycleProgress(int current, int total);
    void showUnsaved(const AnalysisRecord &record);

signals:
    void applyRequested();

private:
    void onSelectionChanged();
    void showRecord(const AnalysisRecord &r);

    QComboBox *m_records = nullptr;
    QTableWidget *m_scores = nullptr;
    QLabel *m_avg = nullptr;
    QPlainTextEdit *m_prompt = nullptr;
    QPushButton *m_applyBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QSpinBox *m_cycles = nullptr;
    QLabel *m_cycleProgress = nullptr;
};

#endif // ANALYSISTAB_H
