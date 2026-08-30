#include "analysistab.h"

#include "core/appsettings.h"
#include "core/sessionstore.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QTableWidgetItem *textItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text.isEmpty() ? QStringLiteral("—") : text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
    return item;
}

} // namespace

AnalysisTab::AnalysisTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    auto *pickRow = new QHBoxLayout;
    pickRow->addWidget(new QLabel(QStringLiteral("Разбор:")));
    m_records = new QComboBox;
    m_records->setMinimumWidth(360);
    m_records->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_records->setMinimumContentsLength(40);
    pickRow->addWidget(m_records, 1);
    root->addLayout(pickRow);

    m_scores = new QTableWidget(5, 4);
    m_scores->setHorizontalHeaderLabels({
        QStringLiteral("Критерий"),
        QStringLiteral("Оценка"),
        QStringLiteral("Ошибка"),
        QStringLiteral("Рекомендация"),
    });
    m_scores->verticalHeader()->setVisible(false);
    m_scores->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_scores->setSelectionMode(QAbstractItemView::NoSelection);
    m_scores->setWordWrap(true);
    m_scores->setTextElideMode(Qt::ElideNone);
    const QStringList names = {
        QStringLiteral("Контакт и доверие"),
        QStringLiteral("Выявление потребности"),
        QStringLiteral("Работа с возражениями"),
        QStringLiteral("Конкретность оффера (цена/срок/следующий шаг)"),
        QStringLiteral("Соответствие типажу покупателя"),
    };
    for (int i = 0; i < names.size(); ++i) {
        m_scores->setItem(i, 0, textItem(names.at(i)));
        m_scores->setItem(i, 1, textItem(QStringLiteral("—")));
        m_scores->setItem(i, 2, textItem(QString()));
        m_scores->setItem(i, 3, textItem(QString()));
    }
    auto *hh = m_scores->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(2, QHeaderView::Stretch);
    hh->setSectionResizeMode(3, QHeaderView::Stretch);
    m_scores->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_scores->setMinimumHeight(220);
    root->addWidget(m_scores, 1);

    m_avg = new QLabel(QStringLiteral("Итоговая оценка: —"));
    m_avg->setStyleSheet(QStringLiteral("font-weight:600;"));
    root->addWidget(m_avg);

    root->addWidget(new QLabel(QStringLiteral("Промпт продавца:")));
    m_prompt = new QPlainTextEdit;
    m_prompt->setReadOnly(true);
    m_prompt->setPlainText(AppSettings::instance().sellerPrompt());
    root->addWidget(m_prompt, 1);

    auto *row = new QHBoxLayout;
    m_applyBtn = new QPushButton(QStringLiteral("Применить рекомендации"));
    row->addWidget(m_applyBtn);
    row->addWidget(new QLabel(QStringLiteral("кол-во циклов:")));
    m_cycles = new QSpinBox;
    m_cycles->setRange(1, 20);
    m_cycles->setValue(1);
    row->addWidget(m_cycles);
    m_cycleProgress = new QLabel(QStringLiteral("итерация: —"));
    m_cycleProgress->setStyleSheet(QStringLiteral("font-weight:600; color:#1e3a5f;"));
    row->addWidget(m_cycleProgress);
    row->addStretch();
    m_clearBtn = new QPushButton(QStringLiteral("Очистить список"));
    m_clearBtn->setVisible(false);
    row->addWidget(m_clearBtn);
    root->addLayout(row);

    connect(m_records, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        onSelectionChanged();
    });
    connect(m_applyBtn, &QPushButton::clicked, this, &AnalysisTab::applyRequested);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("Анализ"),
                                  QStringLiteral("Удалить все сохранённые разборы?"))
            != QMessageBox::Yes)
            return;
        SessionStore::instance().clear();
        reloadFromStore();
    });
}

void AnalysisTab::reloadStarterPrompt()
{
    if (selectedRecord().dialogId <= 0)
        m_prompt->setPlainText(AppSettings::instance().sellerPrompt());
}

void AnalysisTab::reloadFromStore()
{
    const int keepId = selectedRecord().dialogId;
    QSignalBlocker b(m_records);
    m_records->clear();
    const auto &all = SessionStore::instance().records();
    for (int i = 0; i < all.size(); ++i) {
        const bool seriesStart = (i == 0) || all.at(i).seriesId != all.at(i - 1).seriesId;
        addRecord(all.at(i), seriesStart);
    }
    if (keepId > 0)
        selectDialogId(keepId);
    else if (m_records->count() > 0)
        m_records->setCurrentIndex(m_records->count() - 1);
    onSelectionChanged();
}

void AnalysisTab::addRecord(const AnalysisRecord &record, bool seriesStart)
{
    const QString line = QStringLiteral("%1#%2  %3  |  %4  |  ср. %5")
                             .arg(seriesStart ? QStringLiteral("новая ") : QString())
                             .arg(record.dialogId)
                             .arg(record.productItem, record.buyerType)
                             .arg(record.average, 0, 'f', 1);
    m_records->addItem(line, record.dialogId);
    m_records->setItemData(m_records->count() - 1, record.sellerPrompt.left(400), Qt::ToolTipRole);
    m_records->setCurrentIndex(m_records->count() - 1);
}

void AnalysisTab::selectDialogId(int dialogId)
{
    for (int i = 0; i < m_records->count(); ++i) {
        if (m_records->itemData(i).toInt() == dialogId) {
            m_records->setCurrentIndex(i);
            return;
        }
    }
}

AnalysisRecord AnalysisTab::selectedRecord() const
{
    if (!m_records || m_records->currentIndex() < 0)
        return SessionStore::instance().lastRecord();
    return SessionStore::instance().recordById(m_records->currentData().toInt());
}

QString AnalysisTab::currentPrompt() const
{
    const QString text = m_prompt ? m_prompt->toPlainText() : QString();
    return text.trimmed().isEmpty() ? AppSettings::instance().sellerPrompt() : text;
}

int AnalysisTab::cycleCount() const
{
    return m_cycles->value();
}

void AnalysisTab::showUnsaved(const AnalysisRecord &record)
{
    showRecord(record);
}

void AnalysisTab::setBusy(bool busy)
{
    m_applyBtn->setEnabled(!busy);
    m_clearBtn->setEnabled(!busy);
    m_cycles->setEnabled(!busy);
    if (m_records)
        m_records->setEnabled(!busy);
}

void AnalysisTab::setCycleProgress(int current, int total)
{
    if (!m_cycleProgress)
        return;
    if (total <= 0 || current <= 0) {
        m_cycleProgress->setText(QStringLiteral("итерация: —"));
        return;
    }
    m_cycleProgress->setText(QStringLiteral("итерация: %1 из %2").arg(current).arg(total));
}

void AnalysisTab::onSelectionChanged()
{
    showRecord(selectedRecord());
}

void AnalysisTab::showRecord(const AnalysisRecord &r)
{
    if (r.dialogId <= 0) {
        m_avg->setText(QStringLiteral("Итоговая оценка: —"));
        m_prompt->setPlainText(AppSettings::instance().sellerPrompt());
        for (int i = 0; i < 5; ++i) {
            m_scores->setItem(i, 1, textItem(QStringLiteral("—")));
            m_scores->setItem(i, 2, textItem(QString()));
            m_scores->setItem(i, 3, textItem(QString()));
        }
        return;
    }
    for (int i = 0; i < 5; ++i) {
        const int v = r.scores.valueAt(i);
        m_scores->setItem(i, 1, textItem(v > 0 ? QString::number(v) : QStringLiteral("—")));
        m_scores->setItem(i, 2, textItem(r.scores.mistakeAt(i)));
        m_scores->setItem(i, 3, textItem(r.scores.recommendationAt(i)));
    }
    m_scores->resizeRowsToContents();
    m_avg->setText(QStringLiteral("Итоговая оценка: %1   (диалог #%2, %3, %4)")
                       .arg(r.average, 0, 'f', 1)
                       .arg(r.dialogId)
                       .arg(r.productItem, r.buyerType));
    m_prompt->setPlainText(r.newPrompt);
}
