#include "resultstab.h"

#include "core/sessionstore.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStyleOptionHeader>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

class TwoLineHeader : public QHeaderView
{
public:
    explicit TwoLineHeader(QWidget *parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent)
    {
        setDefaultAlignment(Qt::AlignCenter);
        setSectionsClickable(true);
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
    {
        QStyleOptionHeader opt;
        initStyleOption(&opt);
        opt.rect = rect;
        opt.section = logicalIndex;
        opt.position = QStyleOptionHeader::Middle;
        if (logicalIndex == 0)
            opt.position = QStyleOptionHeader::Beginning;
        else if (model() && logicalIndex == model()->columnCount() - 1)
            opt.position = QStyleOptionHeader::End;
        if (isSortIndicatorShown() && sortIndicatorSection() == logicalIndex) {
            opt.sortIndicator = (sortIndicatorOrder() == Qt::AscendingOrder)
                ? QStyleOptionHeader::SortDown
                : QStyleOptionHeader::SortUp;
        }
        const QString text = model()
            ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
            : QString();
        opt.text.clear();
        style()->drawControl(QStyle::CE_Header, &opt, painter, this);
        painter->save();
        painter->setPen(opt.palette.color(QPalette::ButtonText));
        painter->drawText(rect.adjusted(4, 2, -4, -2), Qt::AlignCenter | Qt::TextWordWrap, text);
        painter->restore();
    }

    QSize sectionSizeFromContents(int logicalIndex) const override
    {
        const QString text = model()
            ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
            : QString();
        const QRect br = fontMetrics().boundingRect(QRect(0, 0, 240, 200),
                                                    Qt::AlignCenter | Qt::TextWordWrap, text);
        QSize sz = QHeaderView::sectionSizeFromContents(logicalIndex);
        sz.setWidth(qMax(minimumSectionSize(), br.width() + 16));
        sz.setHeight(qMax(sz.height(), br.height() + 8));
        return sz;
    }
};

QString shortModelName(const QString &fileName)
{
    QString n = fileName.trimmed();
    if (n.isEmpty())
        return QStringLiteral("—");
    if (n.endsWith(QStringLiteral(".gguf"), Qt::CaseInsensitive))
        n.chop(5);
    return n;
}

} // namespace

ResultsTab::ResultsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    m_table = new QTableWidget;
    m_table->setHorizontalHeader(new TwoLineHeader(m_table));
    m_table->setColumnCount(11);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Услуга/товар"),
        QStringLiteral("Характеристика покупателя"),
        QStringLiteral("Кол-во\nвопросов"),
        QStringLiteral("Время диалога"),
        QStringLiteral("ID диалога"),
        QStringLiteral("Цикл"),
        QStringLiteral("Модель\nдиалога"),
        QStringLiteral("Модель\nанализатора"),
        QStringLiteral("Стартовый промпт"),
        QStringLiteral("Средняя\nоценка"),
        QStringLiteral("Δ оценки"),
    });
    auto *hh = m_table->horizontalHeader();
    hh->setStretchLastSection(false);
    hh->setCascadingSectionResizes(false);
    hh->setMinimumSectionSize(36);
    hh->setDefaultAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    hh->setMinimumHeight(hh->fontMetrics().lineSpacing() * 2 + 10);
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setSectionResizeMode(8, QHeaderView::Stretch);
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
    connect(hh, &QHeaderView::sectionResized, this, [this](int, int, int) {
        rememberColumnWidths();
    });
    loadColumnWidths();
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
            if (col != 0 && col != 1 && col != 6 && col != 7 && col != 8)
                item->setTextAlignment(Qt::AlignCenter);
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

        set(6, shortModelName(r.dialogModel), r.dialogModel);
        set(7, shortModelName(r.analyzerModel), r.analyzerModel);

        QString preview = r.sellerPrompt.simplified();
        if (preview.size() > 80)
            preview = preview.left(77) + QStringLiteral("…");
        auto *promptItem = new QTableWidgetItem(preview);
        promptItem->setToolTip(r.sellerPrompt);
        promptItem->setData(Qt::UserRole, r.sellerPrompt);
        m_table->setItem(i, 8, promptItem);

        set(9, QString::number(r.average, 'f', 1));

        auto *deltaItem = new QTableWidgetItem;
        deltaItem->setTextAlignment(Qt::AlignCenter);
        if (seriesStart) {
            deltaItem->setText(QStringLiteral("—"));
            deltaItem->setForeground(QColor(QStringLiteral("#64748b")));
            deltaItem->setToolTip(QStringLiteral("Первый цикл серии — сравнивать не с чем."));
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
            deltaItem->setToolTip(QStringLiteral("Разница со средним предыдущего цикла этой серии."));
        }
        m_table->setItem(i, 10, deltaItem);
    }
    applyColumnLayout();
    if (m_clearBtn)
        m_clearBtn->setEnabled(!rows.isEmpty());
}

void ResultsTab::onCellDoubleClicked(int row, int column)
{
    if (column != 8)
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

void ResultsTab::loadColumnWidths()
{
    QSettings s;
    const QVariantList list = s.value(QStringLiteral("results/colWidths")).toList();
    m_colWidths.clear();
    if (!m_table || list.size() != m_table->columnCount())
        return;
    m_colWidths.reserve(list.size());
    for (const QVariant &v : list)
        m_colWidths.push_back(qMax(0, v.toInt()));
}

void ResultsTab::rememberColumnWidths()
{
    if (m_applyingColumns || !m_table)
        return;
    m_colWidths.resize(m_table->columnCount());
    for (int c = 0; c < m_table->columnCount(); ++c)
        m_colWidths[c] = m_table->columnWidth(c);
    QVariantList list;
    for (int w : m_colWidths)
        list << w;
    QSettings s;
    s.setValue(QStringLiteral("results/colWidths"), list);
}

void ResultsTab::applyColumnLayout()
{
    if (!m_table)
        return;
    auto *hh = m_table->horizontalHeader();
    m_applyingColumns = true;
    const bool restore = (m_colWidths.size() == m_table->columnCount());
    if (!restore) {
        hh->setSectionResizeMode(QHeaderView::ResizeToContents);
        hh->setSectionResizeMode(8, QHeaderView::Stretch);
        m_colWidths.resize(m_table->columnCount());
        for (int c = 0; c < m_table->columnCount(); ++c)
            m_colWidths[c] = m_table->columnWidth(c);
    }
    for (int c = 0; c < m_table->columnCount(); ++c) {
        if (c == 8)
            continue;
        hh->setSectionResizeMode(c, QHeaderView::Interactive);
        const int w = m_colWidths.at(c);
        if (w > 0)
            m_table->setColumnWidth(c, w);
    }
    hh->setSectionResizeMode(8, QHeaderView::Stretch);
    m_applyingColumns = false;
}
