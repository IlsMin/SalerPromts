#ifndef DIALOGTAB_H
#define DIALOGTAB_H

#include "core/types.h"

#include <QWidget>

class QComboBox;
class QPushButton;
class QTextBrowser;
class QLabel;
class QSpinBox;

class DialogTab : public QWidget
{
    Q_OBJECT

public:
    explicit DialogTab(QWidget *parent = nullptr);

    void reloadProducts();
    void reloadBuyers();
    CatalogItem selectedProduct() const;
    QString buyerComboKey() const;
    CatalogItem resolveBuyer() const;
    int targetPairs() const;
    void setRunning(bool running);
    void clearChat();
    void setBuyerInfo(const CatalogItem &buyer);
    void appendTurn(const DialogTurn &turn);

signals:
    void startRequested();
    void stopRequested();

private:
    QComboBox *m_productCombo = nullptr;
    QComboBox *m_buyerCombo = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QSpinBox *m_pairs = nullptr;
    QLabel *m_buyerInfo = nullptr;
    QTextBrowser *m_chat = nullptr;
};

#endif // DIALOGTAB_H
