#include "mainwindow.h"

#include "core/appsettings.h"
#include "core/catalogs.h"
#include "core/sessionstore.h"
#include "services/analyzer.h"
#include "services/dialogengine.h"
#include "services/localllm.h"
#include "tabs/analysistab.h"
#include "tabs/dialogtab.h"
#include "tabs/resultstab.h"
#include "tabs/settingstab.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QUuid>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("SalerPromts — улучшение продающего промпта"));
    resize(1100, 760);

    m_llm = new LocalLlm(this);
    m_dialogEngine = new DialogEngine(m_llm, this);
    m_analyzer = new Analyzer(m_llm, this);

    m_tabs = new QTabWidget(this);
    m_dialogTab = new DialogTab(m_tabs);
    m_analysisTab = new AnalysisTab(m_tabs);
    m_resultsTab = new ResultsTab(m_tabs);
    m_settingsTab = new SettingsTab(m_tabs);
    m_tabs->addTab(m_dialogTab, QStringLiteral("Диалог"));
    m_tabs->addTab(m_analysisTab, QStringLiteral("Анализ"));
    m_tabs->addTab(m_resultsTab, QStringLiteral("Итоги"));
    m_tabs->addTab(m_settingsTab, QStringLiteral("Настройки"));
    setCentralWidget(m_tabs);
    statusBar()->showMessage(QStringLiteral("Загрузка…"));

    connect(m_dialogTab, &DialogTab::startRequested, this, &MainWindow::startFreshDialog);
    connect(m_dialogTab, &DialogTab::stopRequested, this, &MainWindow::stopCurrentRun);
    connect(m_analysisTab, &AnalysisTab::applyRequested, this, &MainWindow::startImprovementCycles);
    connect(m_settingsTab, &SettingsTab::applyRequested, this, [this]() {
        if (m_busy)
            return;
        m_llm->preloadFromSettings();
        m_analysisTab->reloadStarterPrompt();
    });
    connect(m_dialogEngine, &DialogEngine::turnReady, m_dialogTab, &DialogTab::appendTurn);
    connect(m_dialogEngine, &DialogEngine::progress, this, [this](int, int, const QString &s) {
        setBusy(true, withCyclePrefix(s));
    });
    connect(m_dialogEngine, &DialogEngine::finished, this, &MainWindow::onDialogFinished);
    connect(m_dialogEngine, &DialogEngine::failed, this, [this](const QString &e) {
        setBusy(false, e);
        QMessageBox::warning(this, QStringLiteral("Диалог"), e);
    });
    connect(m_analyzer, &Analyzer::progress, this, [this](const QString &s) {
        setBusy(true, withCyclePrefix(s));
    });
    connect(m_analyzer, &Analyzer::finished, this, &MainWindow::onAnalysisFinished);
    connect(m_analyzer, &Analyzer::failed, this, [this](const QString &e) {
        setBusy(false, e);
        QMessageBox::warning(this, QStringLiteral("Анализ"), e);
    });
    connect(m_llm, &LocalLlm::statusChanged, this, [this](const QString &s) {
        statusBar()->showMessage(s, 8000);
    });
    connect(m_llm, &LocalLlm::serverFailed, this, [this](const QString &e) {
        statusBar()->showMessage(e, 15000);
    });
    connect(&SessionStore::instance(), &SessionStore::recordsChanged, this, [this]() {
        m_resultsTab->reload();
        m_analysisTab->reloadFromStore();
    });

    m_analysisTab->reloadFromStore();
    m_resultsTab->reload();

    const QStringList args = QCoreApplication::arguments();
    if (!args.contains(QStringLiteral("--no-preload"))) {
        QTimer::singleShot(200, this, [this]() {
            m_llm->preloadFromSettings();
        });
    } else {
        statusBar()->showMessage(QStringLiteral("Предзагрузка LLM отключена (--no-preload)."));
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_llm->shutdown();
    QMainWindow::closeEvent(event);
}

void MainWindow::setBusy(bool busy, const QString &status)
{
    m_busy = busy;
    m_dialogTab->setRunning(busy);
    m_analysisTab->setBusy(busy);
    m_settingsTab->setBusy(busy);
    updateCycleUi();
    if (!status.isEmpty())
        statusBar()->showMessage(status);
}

void MainWindow::resetCycleProgress()
{
    m_cyclesLeft = 0;
    m_cycleCurrent = 0;
    m_cycleTotal = 0;
    updateCycleUi();
}

void MainWindow::updateCycleUi()
{
    m_analysisTab->setCycleProgress(m_cycleCurrent, m_cycleTotal);
    const int idx = m_tabs->indexOf(m_analysisTab);
    if (idx < 0)
        return;
    if (m_busy && m_cycleTotal > 0 && m_cycleCurrent > 0)
        m_tabs->setTabText(idx, QStringLiteral("Анализ (%1/%2)").arg(m_cycleCurrent).arg(m_cycleTotal));
    else
        m_tabs->setTabText(idx, QStringLiteral("Анализ"));
}

QString MainWindow::withCyclePrefix(const QString &status) const
{
    if (m_cycleTotal <= 0 || m_cycleCurrent <= 0)
        return status;
    return QStringLiteral("Итерация %1 из %2. %3")
        .arg(m_cycleCurrent)
        .arg(m_cycleTotal)
        .arg(status);
}

void MainWindow::stopCurrentRun()
{
    if (m_dialogEngine->isBusy()) {
        resetCycleProgress();
        const AnalysisRecord rec = m_dialogEngine->finishEarly();
        if (rec.transcript.isEmpty()) {
            setBusy(false, QStringLiteral("Диалог прерван: ещё нет реплик для анализа."));
            return;
        }
        onDialogFinished(rec);
        return;
    }
    if (m_analyzer->isBusy()) {
        resetCycleProgress();
        m_analyzer->abort();
        setBusy(false, QStringLiteral("Анализ прерван."));
        return;
    }
    setBusy(false, QStringLiteral("Остановлено."));
}

void MainWindow::startFreshDialog()
{
    if (m_busy)
        return;
    const CatalogItem product = m_dialogTab->selectedProduct();
    if (product.item.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Диалог"), QStringLiteral("Выберите товар."));
        return;
    }
    resetCycleProgress();
    m_cycleProduct = product;
    m_cyclePrompt = AppSettings::instance().sellerPrompt();
    m_activeSeriesId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_cycleTotal = 1;
    m_cyclesLeft = 1;
    m_cycleCurrent = 1;
    updateCycleUi();
    launchDialog(product, m_cyclePrompt, m_activeSeriesId, 0);
}

void MainWindow::startImprovementCycles()
{
    if (m_busy)
        return;
    AnalysisRecord selected = m_analysisTab->selectedRecord();
    if (selected.dialogId <= 0)
        selected = SessionStore::instance().lastRecord();

    CatalogItem product;
    QString seriesId;
    int nextCycle = 0;
    if (selected.dialogId > 0) {
        product = Catalogs::instance().productByItem(selected.productItem);
        if (product.item.isEmpty()) {
            product.item = selected.productItem;
            product.descr = selected.productDescr;
        }
        m_cyclePrompt = Catalogs::keepAsSellerTemplate(
            selected.newPrompt,
            Catalogs::keepAsSellerTemplate(selected.sellerPromptTemplate,
                                           AppSettings::instance().sellerPrompt()));
        seriesId = selected.seriesId;
        nextCycle = selected.cycleIndex + 1;
    } else {
        product = m_dialogTab->selectedProduct();
        if (product.item.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Анализ"),
                                 QStringLiteral("Выберите товар на вкладке «Диалог»."));
            return;
        }
        m_cyclePrompt = Catalogs::keepAsSellerTemplate(
            m_analysisTab->currentPrompt(), AppSettings::instance().sellerPrompt());
        if (m_cyclePrompt.trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Анализ"),
                                 QStringLiteral("Нет промпта продавца. Задайте его в настройках."));
            return;
        }
        seriesId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    m_cycleProduct = product;
    m_activeSeriesId = seriesId;
    m_cycleTotal = qMax(1, m_analysisTab->cycleCount());
    m_cyclesLeft = m_cycleTotal;
    m_cycleCurrent = 1;
    updateCycleUi();
    launchDialog(product, m_cyclePrompt, m_activeSeriesId, nextCycle);
}

void MainWindow::launchDialog(const CatalogItem &product,
                              const QString &sellerPrompt,
                              const QString &seriesId,
                              int cycleIndex)
{
    const CatalogItem buyer = Catalogs::instance().randomCustomer();
    const QString sellerTemplate = Catalogs::keepAsSellerTemplate(
        sellerPrompt, AppSettings::instance().sellerPrompt());

    m_tabs->setCurrentWidget(m_dialogTab);
    m_dialogTab->clearChat();
    m_dialogTab->setBuyerInfo(buyer);
    setBusy(true, QStringLiteral("Старт диалога: %1 ↔ %2").arg(product.item, buyer.item));
    const int pairs = m_dialogTab->targetPairs();
    AppSettings::instance().setTargetPairs(pairs);
    AppSettings::instance().sync();
    m_dialogEngine->start(product, buyer, sellerTemplate,
                          AppSettings::instance().buyerPrompt(), seriesId, cycleIndex, pairs);
}

void MainWindow::onDialogFinished(const AnalysisRecord &record)
{
    AnalysisRecord copy = record;
    copy.dialogId = SessionStore::instance().nextDialogId();
    const int analysisIndex = m_tabs->indexOf(m_analysisTab);
    if (analysisIndex >= 0)
        m_tabs->setCurrentIndex(analysisIndex);
    setBusy(true, withCyclePrefix(
                      QStringLiteral("Диалог #%1 завершён, запуск анализа…").arg(copy.dialogId)));
    m_analyzer->analyze(copy);
}

void MainWindow::onAnalysisFinished(const AnalysisRecord &record)
{
    if (!SessionStore::instance().append(record)) {
        m_cyclesLeft = 0;
        m_analysisTab->showUnsaved(record);
        setBusy(false, QStringLiteral("Анализ #%1 не сохранён: ответ модели не разобран.")
                           .arg(record.dialogId));
        QMessageBox::warning(this, QStringLiteral("Анализ"),
                             QStringLiteral(
                                 "Ответ анализатора не разобран. "
                                 "Запись не добавлена в «Итоги» и не сохранена. "
                                 "Можно запустить диалог ещё раз."));
        return;
    }
    m_analysisTab->reloadFromStore();
    m_analysisTab->selectDialogId(record.dialogId);
    m_resultsTab->reload();
    m_cyclePrompt = Catalogs::keepAsSellerTemplate(
        record.newPrompt,
        Catalogs::keepAsSellerTemplate(record.sellerPromptTemplate,
                                       AppSettings::instance().sellerPrompt()));
    continueCyclesIfNeeded();
}

void MainWindow::continueCyclesIfNeeded()
{
    if (m_cyclesLeft > 0)
        --m_cyclesLeft;
    if (m_cyclesLeft > 0) {
        ++m_cycleCurrent;
        const AnalysisRecord last = SessionStore::instance().lastRecord();
        updateCycleUi();
        setBusy(true, withCyclePrefix(
                          QStringLiteral("Следующий цикл улучшения, осталось %1…").arg(m_cyclesLeft)));
        launchDialog(m_cycleProduct, m_cyclePrompt, m_activeSeriesId, last.cycleIndex + 1);
        return;
    }
    resetCycleProgress();
    setBusy(false, QStringLiteral("Готово."));
}
