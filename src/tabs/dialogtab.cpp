#include "dialogtab.h"

#include "core/appsettings.h"
#include "core/catalogs.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
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
    m_pairs->setRange(3, 20);
    m_pairs->setValue(5);
    m_pairs->setToolTip(QStringLiteral("Сколько пар реплик в предстоящем диалоге"));
    row->addWidget(m_pairs);
    root->addLayout(row);

    auto *buyerRow = new QHBoxLayout;
    buyerRow->addWidget(new QLabel(QStringLiteral("Покупатель:")));
    m_buyerCombo = new QComboBox;
    m_buyerCombo->setMinimumWidth(280);
    m_buyerCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_buyerCombo->setMinimumContentsLength(24);
    buyerRow->addWidget(m_buyerCombo, 1);
    root->addLayout(buyerRow);

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
    connect(m_buyerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const QString name = m_buyerCombo->currentData().toString();
        if (name.isEmpty())
            setBuyerInfo({});
        else
            setBuyerInfo(Catalogs::instance().customerByItem(name));
        auto &cfg = AppSettings::instance();
        cfg.setBuyerType(name);
        cfg.sync();
    });
    reloadProducts();
    reloadBuyers();
}

void DialogTab::reloadBuyers()
{
    const QString current = m_buyerCombo->currentData().toString();
    const QString want = current.isEmpty() ? AppSettings::instance().buyerType() : current;
    QSignalBlocker block(m_buyerCombo);
    m_buyerCombo->clear();
    m_buyerCombo->addItem(QStringLiteral("случайный (один на серию)"), QString());
    m_buyerCombo->setItemData(0, QStringLiteral(
        "На старте серии выбирается один типаж и держится во всех её циклах. "
        "Для сравнения моделей выберите тип вручную."), Qt::ToolTipRole);
    for (const CatalogItem &c : Catalogs::instance().customers()) {
        m_buyerCombo->addItem(c.item, c.item);
        m_buyerCombo->setItemData(m_buyerCombo->count() - 1, c.descr, Qt::ToolTipRole);
    }
    int idx = 0;
    if (!want.isEmpty()) {
        const int found = m_buyerCombo->findData(want);
        if (found >= 0)
            idx = found;
    }
    m_buyerCombo->setCurrentIndex(idx);
    const QString name = m_buyerCombo->currentData().toString();
    if (name.isEmpty())
        setBuyerInfo({});
    else
        setBuyerInfo(Catalogs::instance().customerByItem(name));
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
    m_buyerCombo->setEnabled(!running);
    m_pairs->setEnabled(!running);
}

QString DialogTab::buyerComboKey() const
{
    return m_buyerCombo ? m_buyerCombo->currentData().toString() : QString();
}

CatalogItem DialogTab::resolveBuyer() const
{
    const QString name = buyerComboKey();
    if (name.isEmpty())
        return Catalogs::instance().randomCustomer();
    CatalogItem c = Catalogs::instance().customerByItem(name);
    if (c.item.isEmpty())
        return Catalogs::instance().randomCustomer();
    return c;
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
            "Случайный типаж: один выбирается на старте серии и не меняется в её циклах. "
            "Для чистого сравнения моделей выберите покупателя в списке выше."));
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
