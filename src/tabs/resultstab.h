#ifndef RESULTSTAB_H
#define RESULTSTAB_H

#include <QWidget>

class QTableWidget;
class QPushButton;

class ResultsTab : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsTab(QWidget *parent = nullptr);
    void reload();

private:
    void onCellDoubleClicked(int row, int column);
    void onClearClicked();

    QTableWidget *m_table = nullptr;
    QPushButton *m_clearBtn = nullptr;
};

#endif // RESULTSTAB_H
