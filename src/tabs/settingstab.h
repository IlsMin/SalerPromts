#ifndef SETTINGSTAB_H
#define SETTINGSTAB_H

#include <QWidget>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QPlainTextEdit;
class QLabel;
class QPushButton;

class SettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsTab(QWidget *parent = nullptr);
    void loadFromSettings();
    void setBusy(bool busy);

signals:
    void applyRequested();

private:
    void browseModels();
    void browseLlama();
    void rescanModels();
    void refillAnalyzerCombo();
    void updateVramHint();
    void onSave();

    QPushButton *m_saveBtn = nullptr;
    bool m_busy = false;
    QLineEdit *m_modelsDir = nullptr;
    QLineEdit *m_llamaDir = nullptr;
    QComboBox *m_dialogModel = nullptr;
    QComboBox *m_analyzerModel = nullptr;
    QSpinBox *m_dialogGpu = nullptr;
    QSpinBox *m_analyzerGpu = nullptr;
    QSpinBox *m_threads = nullptr;
    QSpinBox *m_pairs = nullptr;
    QSpinBox *m_dialogPort = nullptr;
    QSpinBox *m_analyzerPort = nullptr;
    QPlainTextEdit *m_sellerPrompt = nullptr;
    QPlainTextEdit *m_buyerPrompt = nullptr;
    QLabel *m_deviceHint = nullptr;
    QLabel *m_vramHint = nullptr;
    QLabel *m_status = nullptr;
};

#endif // SETTINGSTAB_H
