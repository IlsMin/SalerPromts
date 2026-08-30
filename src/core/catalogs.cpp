#include "catalogs.h"

#include "apppaths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTextStream>

Catalogs &Catalogs::instance()
{
    static Catalogs s;
    return s;
}

QString Catalogs::defaultSellerPrompt()
{
    const QString path = AppPaths::findDataFile(QStringLiteral("default_seller_prompt.txt"));
    if (!path.isEmpty()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString::fromUtf8(f.readAll()).trimmed();
    }
    return QStringLiteral(
        "Вы — опытный продавец-консультант. Вы общаетесь с живым клиентом: без дисклеймеров, "
        "без упоминания, что вы ИИ, модель или промпт.\n\n"
        "Товар/услуга: {item}\n"
        "Кратко: {item_descr}\n\n"
        "База знаний (используйте ТОЛЬКО эти факты; не выдумывайте цены, сроки, функции, гарантии "
        "и условия, которых здесь нет):\n{item_knowledge}\n\n"
        "Тип покупателя: {buyer_type}\n"
        "Особенности покупателя: {buyer_descr}\n\n"
        "Цели разговора:\n"
        "1. Понять потребность и контекст клиента.\n"
        "2. Спокойно отработать возражения, опираясь на базу знаний.\n"
        "3. Предложить конкретный следующий шаг (демо, расчёт, встреча, пробный период), "
        "когда потребность уже ясна.\n"
        "Не закрывайте сделку жёстко в первых репликах — ведите естественный диалог.\n"
        "Подстраивайте тон и темп под тип покупателя.\n"
        "Отвечайте коротко или средне (2–6 предложений), без эссе.\n"
        "Задавайте уточняющие вопросы, если это уместно.");
}

QString Catalogs::defaultBuyerPrompt()
{
    const QString path = AppPaths::findDataFile(QStringLiteral("default_buyer_prompt.txt"));
    if (!path.isEmpty()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString::fromUtf8(f.readAll()).trimmed();
    }
    return QStringLiteral(
        "Вы играете роль покупателя в учебном диалоге с продавцом. Это ролевая игра: "
        "не выходите из роли и не пишите ремарок «как ИИ».\n\n"
        "Ваш тип: {buyer_type}\n"
        "Как себя вести: {buyer_descr}\n"
        "{buyer_knowledge}\n\n"
        "Тема разговора — {item} ({item_descr}).\n\n"
        "Правила:\n"
        "- Оставайтесь в разговоре до конца сессии. Ранние отказы и «мне неинтересно, до свидания» нежелательны.\n"
        "- Не соглашайтесь купить, подписать договор или оплатить в первых двух своих репликах.\n"
        "- Можете сомневаться, сравнивать, торговаться и задавать вопросы — в рамках своего типажа.\n"
        "- Если типаж «короткий» — отвечайте очень кратко (1–2 фразы). Если «сомневается» — сомневайтесь и уточняйте. "
        "Если «знает, чего хочет» — говорите конкретной потребностью.\n"
        "- Не раскрывайте, что вы играете роль.");
}

QString Catalogs::substitutePlaceholders(QString templ,
                                         const CatalogItem &product,
                                         const CatalogItem &buyer)
{
    templ.replace(QStringLiteral("{item}"), product.item);
    templ.replace(QStringLiteral("{item_descr}"), product.descr);
    templ.replace(QStringLiteral("{item_knowledge}"), product.knowledge);
    templ.replace(QStringLiteral("{buyer_type}"), buyer.item);
    templ.replace(QStringLiteral("{buyer_descr}"), buyer.descr);
    templ.replace(QStringLiteral("{buyer_knowledge}"), buyer.knowledge);
    return templ;
}

bool Catalogs::hasBuyerPlaceholder(const QString &prompt)
{
    return prompt.contains(QStringLiteral("{buyer_type}"));
}

QString Catalogs::keepAsSellerTemplate(const QString &candidate, const QString &fallbackTemplate)
{
    if (hasBuyerPlaceholder(candidate))
        return candidate;
    if (hasBuyerPlaceholder(fallbackTemplate))
        return fallbackTemplate;
    return candidate;
}

CatalogItem Catalogs::parseItem(const QJsonObject &obj, bool asCustomer)
{
    CatalogItem item;
    item.item = obj.value(QStringLiteral("item")).toString().trimmed();
    if (item.item.isEmpty())
        item.item = obj.value(QStringLiteral("type")).toString().trimmed();
    item.descr = obj.value(QStringLiteral("descr")).toString().trimmed();
    item.knowledge = obj.value(QStringLiteral("knowledge")).toString().trimmed();
    Q_UNUSED(asCustomer);
    return item;
}

bool Catalogs::loadList(const QString &fileName, QVector<CatalogItem> *out, bool asCustomer)
{
    const QString path = AppPaths::findDataFile(fileName);
    if (path.isEmpty()) {
        m_error = QStringLiteral("Не найден файл %1 (искали в data/ рядом с exe и в исходниках).").arg(fileName);
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Не удалось открыть %1").arg(path);
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        m_error = QStringLiteral("Ошибка JSON в %1: %2").arg(path, err.errorString());
        return false;
    }
    out->clear();
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const CatalogItem item = parseItem(v.toObject(), asCustomer);
        if (!item.item.isEmpty())
            out->push_back(item);
    }
    return !out->isEmpty();
}

bool Catalogs::load()
{
    m_error.clear();
    if (!loadList(QStringLiteral("products.json"), &m_products, false))
        return false;
    if (!loadList(QStringLiteral("customers.json"), &m_customers, true))
        return false;
    return true;
}

CatalogItem Catalogs::productByItem(const QString &item) const
{
    for (const CatalogItem &p : m_products) {
        if (p.item == item)
            return p;
    }
    return {};
}

CatalogItem Catalogs::randomCustomer() const
{
    if (m_customers.isEmpty())
        return {};
    const int i = int(QRandomGenerator::global()->bounded(m_customers.size()));
    return m_customers.at(i);
}
