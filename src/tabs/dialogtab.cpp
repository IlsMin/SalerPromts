#include "dialogtab.h"

#include "core/catalogs.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QString escapeHtml(const QString &text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

} // namespace

DialogTab::DialogTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Товар / услуга:")));
    m_productCombo = new QComboBox;
    m_productCombo->setMinimumWidth(280);
    row->addWidget(m_productCombo, 1);
    m_startBtn = new QPushButton(QStringLiteral("Начать"));
    row->addWidget(m_startBtn);
    m_stopBtn = new QPushButton(QStringLiteral("Прекратить"));
    m_stopBtn->setEnabled(false);
    row->addWidget(m_stopBtn);
    row->addWidget(new QLabel(QStringLiteral("вопросов:")));
    m_pairs = new QSpinBox;
    m_pairs->setRange(5, 20);
    m_pairs->setValue(5);
    m_pairs->setToolTip(QStringLiteral("Сколько пар реплик в предстоящем диалоге"));
    row->addWidget(m_pairs);
    root->addLayout(row);

    m_buyerInfo = new QLabel;
    m_buyerInfo->setWordWrap(true);
    m_buyerInfo->setTextFormat(Qt::RichText);
    m_buyerInfo->setStyleSheet(QStringLiteral("color:#334155;"));
    setBuyerInfo({});
    root->addWidget(m_buyerInfo);

    m_chat = new QTextBrowser;
    m_chat->setOpenExternalLinks(false);
    root->addWidget(m_chat, 1);

    connect(m_startBtn, &QPushButton::clicked, this, &DialogTab::startRequested);
    connect(m_stopBtn, &QPushButton::clicked, this, &DialogTab::stopRequested);
    reloadProducts();
}

void DialogTab::reloadProducts()
{
    const QString current = m_productCombo->currentData().toString();
    m_productCombo->clear();
    for (const CatalogItem &p : Catalogs::instance().products()) {
        m_productCombo->addItem(p.item, p.item);
        m_productCombo->setItemData(m_productCombo->count() - 1, p.descr, Qt::ToolTipRole);
    }
    if (!current.isEmpty()) {
        const int idx = m_productCombo->findData(current);
        if (idx >= 0)
            m_productCombo->setCurrentIndex(idx);
    }
}

CatalogItem DialogTab::selectedProduct() const
{
    return Catalogs::instance().productByItem(m_productCombo->currentData().toString());
}

void DialogTab::setRunning(bool running)
{
    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    m_productCombo->setEnabled(!running);
    m_pairs->setEnabled(!running);
}

int DialogTab::targetPairs() const
{
    return m_pairs->value();
}

void DialogTab::clearChat()
{
    m_chat->clear();
}

void DialogTab::setBuyerInfo(const CatalogItem &buyer)
{
    if (buyer.item.isEmpty()) {
        m_buyerInfo->setText(QStringLiteral(
            "Тип покупателя выбирается случайно при старте диалога."));
        m_buyerInfo->setToolTip(QString());
        return;
    }
    const QString descr = buyer.descr.trimmed();
    m_buyerInfo->setText(QStringLiteral("<b>Тип покупателя:</b> %1%2")
                             .arg(escapeHtml(buyer.item),
                                  descr.isEmpty()
                                      ? QString()
                                      : QStringLiteral(" — %1").arg(escapeHtml(descr))));
    const QString tip = buyer.knowledge.trimmed().isEmpty() ? buyer.descr : buyer.knowledge;
    m_buyerInfo->setToolTip(tip);
}

void DialogTab::appendTurn(const DialogTurn &turn)
{
    const bool seller = turn.speaker == QStringLiteral("seller");
    const QString color = seller ? QStringLiteral("#1d4ed8") : QStringLiteral("#b45309");
    const QString who = seller ? QStringLiteral("Продавец") : QStringLiteral("Покупатель");
    m_chat->append(QStringLiteral("<p style='margin:8px 0;'><span style='color:%1;'><b>%2</b></span><br>%3</p>")
                       .arg(color, who, escapeHtml(turn.text)));
}
