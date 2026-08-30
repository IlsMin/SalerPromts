#include "mainwindow.h"

#include "core/appsettings.h"
#include "core/catalogs.h"
#include "core/sessionstore.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QTabWidget>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SalerPromts"));
    QApplication::setOrganizationName(QStringLiteral("SalerPromts"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));

    const QStringList args = app.arguments();
    const bool smoke = args.contains(QStringLiteral("--smoke"));

    AppSettings::instance();
    if (!Catalogs::instance().load()) {
        if (!smoke) {
            QMessageBox::critical(nullptr, QStringLiteral("SalerPromts"),
                                  Catalogs::instance().lastError());
        }
    }
    SessionStore::instance().load();

    MainWindow window;
    window.show();

    if (smoke) {
        int tabCount = 0;
        QStringList tabNames;
        for (QTabWidget *tabs : window.findChildren<QTabWidget *>()) {
            tabCount = tabs->count();
            for (int i = 0; i < tabCount; ++i)
                tabNames << tabs->tabText(i);
        }
        QFile f(QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("smoke-result.txt")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QString out;
            out += QStringLiteral("products=%1\n").arg(Catalogs::instance().products().size());
            out += QStringLiteral("customers=%1\n").arg(Catalogs::instance().customers().size());
            out += QStringLiteral("catalog_error=%1\n").arg(Catalogs::instance().lastError());
            out += QStringLiteral("tabs=%1\n").arg(tabCount);
            out += QStringLiteral("tab_names=%1\n").arg(tabNames.join(QLatin1Char('|')));
            f.write(out.toUtf8());
        }
        QTimer::singleShot(1200, &app, &QApplication::quit);
    }

    return app.exec();
}
