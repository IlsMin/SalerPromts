#ifndef CATALOGS_H
#define CATALOGS_H

#include "types.h"

#include <QString>
#include <QVector>

class Catalogs
{
public:
    static Catalogs &instance();

    bool load();
    QString lastError() const { return m_error; }

    const QVector<CatalogItem> &products() const { return m_products; }
    const QVector<CatalogItem> &customers() const { return m_customers; }

    CatalogItem productByItem(const QString &item) const;
    CatalogItem randomCustomer() const;

    static QString defaultSellerPrompt();
    static QString defaultBuyerPrompt();
    static QString substitutePlaceholders(QString templ,
                                          const CatalogItem &product,
                                          const CatalogItem &buyer);
    static bool hasBuyerPlaceholder(const QString &prompt);
    static QString keepAsSellerTemplate(const QString &candidate,
                                        const QString &fallbackTemplate);

private:
    Catalogs() = default;
    bool loadList(const QString &fileName, QVector<CatalogItem> *out, bool asCustomer);
    static CatalogItem parseItem(const QJsonObject &obj, bool asCustomer);

    QVector<CatalogItem> m_products;
    QVector<CatalogItem> m_customers;
    QString m_error;
};

#endif // CATALOGS_H
