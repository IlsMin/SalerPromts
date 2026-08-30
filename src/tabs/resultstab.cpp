#include "resultstab.h"

#include "core/sessionstore.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

ResultsTab::ResultsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    auto *hint = new QLabel(QStringLiteral(
        "Двойной щелчок по стартовому промпту копирует полный текст в буфер. "
        "Δ — к предыдущей строке таблицы (прочерк только у самой первой). "
        "«новая» — старт серии: кнопка «Начать» (один проход) или первый цикл пакета."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#64748b;"));
    root->addWidget(hint);

    m_table = new QTableWidget;
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Услуга/товар"),
        QStringLiteral("Характеристика покупателя"),
        QStringLiteral("Кол-во вопросов"),
        QStringLiteral("Время диалога"),
        QStringLiteral("ID диалога"),
        QStringLiteral("Цикл"),
        QStringLiteral("Стартовый промпт"),
        QStringLiteral("Средняя оценка"),
        QStringLiteral("Δ оценки"),
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setWordWrap(true);
    root->addWidget(m_table, 1);

    auto *row = new QHBoxLayout;
    m_clearBtn = new QPushButton(QStringLiteral("Очистить таблицу"));
    m_clearBtn->setToolTip(QStringLiteral("Удалить все строки из «Итогов» и файл results.json."));
    row->addWidget(m_clearBtn);
    row->addStretch();
    root->addLayout(row);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ResultsTab::onCellDoubleClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &ResultsTab::onClearClicked);
}

void ResultsTab::reload()
{
    const auto &rows = SessionStore::instance().records();
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const AnalysisRecord &r = rows.at(i);
        auto set = [this, i](int col, const QString &text, const QString &tip = QString()) {
            auto *item = new QTableWidgetItem(text);
            item->setToolTip(tip.isEmpty() ? text : tip);
            m_table->setItem(i, col, item);
        };
        set(0, r.productItem, r.productDescr);
        set(1, r.buyerType, r.buyerDescr);
        set(2, QString::number(r.pairCount));
        const double sec = r.elapsedMs / 1000.0;
        set(3, sec >= 60 ? QStringLiteral("%1 мин %2 с").arg(int(sec / 60)).arg(int(sec) % 60)
                         : QStringLiteral("%1 с").arg(sec, 0, 'f', 1));
        set(4, QString::number(r.dialogId));

        const bool seriesStart = (i == 0) || r.seriesId != rows.at(i - 1).seriesId;
        if (seriesStart)
            set(5, QStringLiteral("новая"),
                QStringLiteral("Старт серии (кнопка «Начать» или первый проход пакета циклов)."));
        else
            set(5, QString::number(r.cycleIndex + 1),
                QStringLiteral("Продолжение серии, цикл %1.").arg(r.cycleIndex + 1));

        QString preview = r.sellerPrompt.simplified();
        if (preview.size() > 80)
            preview = preview.left(77) + QStringLiteral("…");
        auto *promptItem = new QTableWidgetItem(preview);
        promptItem->setToolTip(r.sellerPrompt);
        promptItem->setData(Qt::UserRole, r.sellerPrompt);
        m_table->setItem(i, 6, promptItem);

        set(7, QString::number(r.average, 'f', 1));

        auto *deltaItem = new QTableWidgetItem;
        if (i == 0) {
            deltaItem->setText(QStringLiteral("—"));
            deltaItem->setForeground(QColor(QStringLiteral("#64748b")));
            deltaItem->setToolTip(QStringLiteral("Первая запись в таблице — сравнивать не с чем."));
        } else {
            const double delta = r.average - rows.at(i - 1).average;
            const QString sign = delta > 0 ? QStringLiteral("+") : QString();
            deltaItem->setText(sign + QString::number(delta, 'f', 1));
            if (delta > 0.05)
                deltaItem->setForeground(QColor(QStringLiteral("#15803d")));
            else if (delta < -0.05)
                deltaItem->setForeground(QColor(QStringLiteral("#b91c1c")));
            else
                deltaItem->setForeground(QColor(QStringLiteral("#64748b")));
        }
        m_table->setItem(i, 8, deltaItem);
    }
    m_table->resizeColumnsToContents();
    if (m_table->columnWidth(6) < 180)
        m_table->setColumnWidth(6, 180);
    if (m_clearBtn)
        m_clearBtn->setEnabled(!rows.isEmpty());
}

void ResultsTab::onCellDoubleClicked(int row, int column)
{
    if (column != 6)
        return;
    auto *item = m_table->item(row, column);
    if (!item)
        return;
    const QString full = item->data(Qt::UserRole).toString();
    if (!full.isEmpty())
        QApplication::clipboard()->setText(full);
}

void ResultsTab::onClearClicked()
{
    if (SessionStore::instance().records().isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("Итоги"),
                              QStringLiteral("Очистить таблицу? Все сохранённые диалоги и разборы будут удалены."))
        != QMessageBox::Yes)
        return;
    SessionStore::instance().clear();
}
