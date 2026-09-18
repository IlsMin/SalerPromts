#ifndef RESULTSTAB_H
#define RESULTSTAB_H

#include <QVector>
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
    void loadColumnWidths();
    void rememberColumnWidths();
    void applyColumnLayout();

    QTableWidget *m_table = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QVector<int> m_colWidths;
    bool m_applyingColumns = false;
};

#endif // RESULTSTAB_H
