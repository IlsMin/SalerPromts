#include "settingstab.h"

#include "core/apppaths.h"
#include "core/appsettings.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QWidget *browseRow(QLineEdit *edit, QPushButton *btn)
{
    auto *w = new QWidget;
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(edit, 1);
    lay->addWidget(btn);
    return w;
}

void selectByFileName(QComboBox *combo, const QString &fileName)
{
    const int idx = combo->findData(fileName);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
    else if (combo->count() > 0)
        combo->setCurrentIndex(0);
}

} // namespace

SettingsTab::SettingsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    auto *root = new QVBoxLayout(content);

    auto *paths = new QGroupBox(QStringLiteral("Каталоги"));
    auto *pathsForm = new QFormLayout(paths);
    m_modelsDir = new QLineEdit;
    auto *modelsBrowse = new QPushButton(QStringLiteral("Обзор…"));
    pathsForm->addRow(QStringLiteral("Каталог моделей GGUF:"), browseRow(m_modelsDir, modelsBrowse));
    m_llamaDir = new QLineEdit;
    auto *llamaBrowse = new QPushButton(QStringLiteral("Обзор…"));
    pathsForm->addRow(QStringLiteral("Каталог llama (llama-server.exe):"), browseRow(m_llamaDir, llamaBrowse));
    root->addWidget(paths);

    auto *models = new QGroupBox(QStringLiteral("Модели"));
    auto *modelsForm = new QFormLayout(models);
    m_dialogModel = new QComboBox;
    m_analyzerModel = new QComboBox;
    modelsForm->addRow(QStringLiteral("Модель диалога:"), m_dialogModel);
    modelsForm->addRow(QStringLiteral("Модель анализатора:"), m_analyzerModel);
    auto *filterHint = new QLabel(QStringLiteral(
        "Диалог и анализ идут по очереди: перед разбором диалог выгружается, GPU свободен. "
        "Отдельной модели анализатора можно дать 99 слоёв GPU. "
        "Одна и та же модель — ещё проще: один сервер, без перезагрузки."));
    filterHint->setWordWrap(true);
    filterHint->setStyleSheet(QStringLiteral("color:#64748b;"));
    modelsForm->addRow(filterHint);
    root->addWidget(models);

    auto *gpu = new QGroupBox(QStringLiteral("Выполнение"));
    auto *gpuForm = new QFormLayout(gpu);
    m_dialogGpu = new QSpinBox;
    m_dialogGpu->setRange(0, 99);
    m_analyzerGpu = new QSpinBox;
    m_analyzerGpu->setRange(0, 99);
    m_threads = new QSpinBox;
    m_threads->setRange(1, 32);
    m_pairs = new QSpinBox;
    m_pairs->setRange(5, 20);
    m_dialogPort = new QSpinBox;
    m_dialogPort->setRange(1024, 65535);
    m_analyzerPort = new QSpinBox;
    m_analyzerPort->setRange(1024, 65535);
    gpuForm->addRow(QStringLiteral("Слои GPU диалога (0 = CPU, 99 = вся на GPU):"), m_dialogGpu);
    gpuForm->addRow(QStringLiteral("Слои GPU анализатора (0 = CPU, 99 = вся на GPU):"), m_analyzerGpu);
    m_deviceHint = new QLabel;
    m_deviceHint->setWordWrap(true);
    m_deviceHint->setStyleSheet(QStringLiteral("color:#334155;"));
    gpuForm->addRow(m_deviceHint);
    gpuForm->addRow(QStringLiteral("Потоки (-t):"), m_threads);
    gpuForm->addRow(QStringLiteral("Пар реплик в диалоге:"), m_pairs);
    gpuForm->addRow(QStringLiteral("Порт диалога:"), m_dialogPort);
    gpuForm->addRow(QStringLiteral("Порт анализатора:"), m_analyzerPort);
    m_vramHint = new QLabel;
    m_vramHint->setWordWrap(true);
    m_vramHint->setStyleSheet(QStringLiteral("color:#b45309;"));
    gpuForm->addRow(m_vramHint);
    root->addWidget(gpu);

    auto *prompts = new QGroupBox(QStringLiteral("Промпты"));
    auto *promptsLay = new QVBoxLayout(prompts);
    promptsLay->addWidget(new QLabel(QStringLiteral(
        "Плейсхолдеры продавца: {item} {item_descr} {item_knowledge} {buyer_type} {buyer_descr}")));
    m_sellerPrompt = new QPlainTextEdit;
    m_sellerPrompt->setMinimumHeight(180);
    promptsLay->addWidget(m_sellerPrompt);
    promptsLay->addWidget(new QLabel(QStringLiteral("Системный промпт покупателя (роль, отказы нежелательны):")));
    m_buyerPrompt = new QPlainTextEdit;
    m_buyerPrompt->setMinimumHeight(140);
    promptsLay->addWidget(m_buyerPrompt);
    root->addWidget(prompts);

    m_saveBtn = new QPushButton(QStringLiteral("Сохранить и применить"));
    m_saveBtn->setToolTip(QStringLiteral("Сохранить настройки и перезапустить llama-server."));
    root->addWidget(m_saveBtn);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    root->addStretch();

    scroll->setWidget(content);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(modelsBrowse, &QPushButton::clicked, this, &SettingsTab::browseModels);
    connect(llamaBrowse, &QPushButton::clicked, this, &SettingsTab::browseLlama);
    connect(m_modelsDir, &QLineEdit::editingFinished, this, &SettingsTab::rescanModels);
    connect(m_dialogModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refillAnalyzerCombo();
        updateVramHint();
    });
    connect(m_analyzerModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateVramHint();
    });
    connect(m_dialogGpu, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { updateVramHint(); });
    connect(m_analyzerGpu, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { updateVramHint(); });
    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsTab::onSave);

    loadFromSettings();
}

void SettingsTab::browseModels()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Каталог GGUF"), m_modelsDir->text());
    if (dir.isEmpty())
        return;
    m_modelsDir->setText(QDir(dir).absolutePath());
    rescanModels();
}

void SettingsTab::browseLlama()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Каталог llama-server"), m_llamaDir->text());
    if (dir.isEmpty())
        return;
    m_llamaDir->setText(QDir(dir).absolutePath());
}

void SettingsTab::rescanModels()
{
    const QString keepDialog = m_dialogModel->currentData().toString();
    const QString keepAnalyzer = m_analyzerModel->currentData().toString();
    const auto models = AppPaths::discoverGgufModels(m_modelsDir->text());

    QSignalBlocker b1(m_dialogModel);
    m_dialogModel->clear();
    for (const GgufModelInfo &m : models) {
        m_dialogModel->addItem(
            QStringLiteral("%1 (%2)").arg(m.fileName, AppPaths::formatDiskSize(m.sizeBytes)),
            m.fileName);
    }
    if (!keepDialog.isEmpty())
        selectByFileName(m_dialogModel, keepDialog);
    else
        selectByFileName(m_dialogModel, AppSettings::instance().dialogModel());

    Q_UNUSED(keepAnalyzer);
    refillAnalyzerCombo();
    if (!keepAnalyzer.isEmpty())
        selectByFileName(m_analyzerModel, keepAnalyzer);
    updateVramHint();
}

void SettingsTab::refillAnalyzerCombo()
{
    const QString keep = m_analyzerModel->currentData().toString().isEmpty()
        ? AppSettings::instance().analyzerModel()
        : m_analyzerModel->currentData().toString();
    const auto models = AppPaths::discoverGgufModels(m_modelsDir->text());
    qint64 dialogSize = 0;
    const QString dialogName = m_dialogModel->currentData().toString();
    for (const GgufModelInfo &m : models) {
        if (m.fileName.compare(dialogName, Qt::CaseInsensitive) == 0)
            dialogSize = m.sizeBytes;
    }

    QSignalBlocker b(m_analyzerModel);
    m_analyzerModel->clear();
    for (const GgufModelInfo &m : models) {
        QString label = QStringLiteral("%1 (%2)").arg(m.fileName, AppPaths::formatDiskSize(m.sizeBytes));
        if (dialogSize > 0 && m.sizeBytes > dialogSize)
            label += QStringLiteral(" — тяжелее диалога");
        m_analyzerModel->addItem(label, m.fileName);
    }
    selectByFileName(m_analyzerModel, keep);
}

void SettingsTab::updateVramHint()
{
    const auto models = AppPaths::discoverGgufModels(m_modelsDir->text());
    QStringList warns;
    auto find = [&models](const QString &name) -> GgufModelInfo {
        for (const auto &m : models) {
            if (m.fileName.compare(name, Qt::CaseInsensitive) == 0)
                return m;
        }
        return {};
    };
    const GgufModelInfo d = find(m_dialogModel->currentData().toString());
    const QString analyzerName = m_analyzerModel->currentData().toString();

    QStringList devices;
    const QString dialogHint = AppPaths::gpuOffloadHint(d.fileName, m_dialogGpu->value());
    if (!dialogHint.isEmpty())
        devices << QStringLiteral("Диалог: ") + dialogHint;
    const bool same = analyzerName.compare(d.fileName, Qt::CaseInsensitive) == 0;
    if (m_analyzerGpu)
        m_analyzerGpu->setEnabled(!same);
    if (same) {
        devices << QStringLiteral("Анализатор: та же модель и тот же сервер, что диалог.");
    } else {
        const QString analyzerHint = AppPaths::gpuOffloadHint(analyzerName, m_analyzerGpu->value());
        if (!analyzerHint.isEmpty())
            devices << QStringLiteral("Анализатор: ") + analyzerHint;
        devices << QStringLiteral(
            "Перед анализом диалог выгружается — слои GPU анализатора не делят карту с диалогом.");
    }
    if (m_deviceHint)
        m_deviceHint->setText(devices.join(QStringLiteral("\n")));

    const QString w1 = AppPaths::vramWarning(d.fileName, d.sizeBytes, m_dialogGpu->value(), 4096);
    if (!w1.isEmpty())
        warns << w1;
    if (!same) {
        const GgufModelInfo a = find(analyzerName);
        const QString w2 = AppPaths::vramWarning(a.fileName, a.sizeBytes, m_analyzerGpu->value(), 4096);
        if (!w2.isEmpty())
            warns << w2;
    }
    m_vramHint->setText(warns.join(QStringLiteral("\n")));
}

void SettingsTab::loadFromSettings()
{
    const auto &cfg = AppSettings::instance();
    QSignalBlocker b1(m_modelsDir);
    m_modelsDir->setText(cfg.modelsDir());
    m_llamaDir->setText(cfg.llamaBinDir());
    m_dialogGpu->setValue(cfg.dialogGpuLayers());
    m_analyzerGpu->setValue(cfg.analyzerGpuLayers());
    m_threads->setValue(cfg.threads());
    m_pairs->setValue(cfg.targetPairs());
    m_dialogPort->setValue(cfg.dialogPort());
    m_analyzerPort->setValue(cfg.analyzerPort());
    m_sellerPrompt->setPlainText(cfg.sellerPrompt());
    m_buyerPrompt->setPlainText(cfg.buyerPrompt());
    rescanModels();
    selectByFileName(m_dialogModel, cfg.dialogModel());
    refillAnalyzerCombo();
    selectByFileName(m_analyzerModel, cfg.analyzerModel());
    updateVramHint();
}

void SettingsTab::setBusy(bool busy)
{
    m_busy = busy;
    if (!m_saveBtn)
        return;
    m_saveBtn->setEnabled(!busy);
    m_saveBtn->setToolTip(busy
        ? QStringLiteral("Сейчас идёт диалог или цикл. Дождитесь окончания или нажмите «Прекратить» — "
                         "иначе смена модели оборвёт llama-server.")
        : QStringLiteral("Сохранить настройки и перезапустить llama-server."));
}

void SettingsTab::onSave()
{
    if (m_busy)
        return;
    auto &cfg = AppSettings::instance();
    cfg.setModelsDir(m_modelsDir->text());
    cfg.setLlamaBinDir(m_llamaDir->text());
    cfg.setDialogModel(m_dialogModel->currentData().toString());
    cfg.setAnalyzerModel(m_analyzerModel->currentData().toString());
    cfg.setDialogGpuLayers(m_dialogGpu->value());
    cfg.setAnalyzerGpuLayers(m_analyzerGpu->value());
    cfg.setThreads(m_threads->value());
    cfg.setTargetPairs(m_pairs->value());
    cfg.setDialogPort(m_dialogPort->value());
    cfg.setAnalyzerPort(m_analyzerPort->value());
    cfg.setSellerPrompt(m_sellerPrompt->toPlainText());
    cfg.setBuyerPrompt(m_buyerPrompt->toPlainText());

    if (cfg.sameModel() && cfg.dialogPort() == cfg.analyzerPort()) {
        // ok — one server
    } else if (cfg.dialogPort() == cfg.analyzerPort()) {
        QMessageBox::warning(this, QStringLiteral("Порты"),
                             QStringLiteral("Для двух разных моделей порты диалога и анализатора должны различаться."));
        return;
    }

    const QString hint = m_vramHint->text();
    if (!hint.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Оценка VRAM"), hint);
    }

    const QString exe = AppPaths::llamaServerPath(cfg.llamaBinDir());
    if (!QFileInfo::exists(exe)) {
        QMessageBox::warning(this, QStringLiteral("llama-server"),
                             QStringLiteral("Не найден llama-server.exe в %1").arg(cfg.llamaBinDir()));
    }

    cfg.sync();
    m_status->setText(QStringLiteral("Сохранено. Запускаю llama-server…"));
    emit applyRequested();
}
