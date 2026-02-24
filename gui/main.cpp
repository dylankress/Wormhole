#include <QApplication>
#include <QIcon>
#include <QLockFile>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName(QStringLiteral("Wormhole"));
    app.setOrganizationName(QStringLiteral("Wormhole"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // Ensure single instance
    QString lockPath = QDir::homePath() + "/.wormhole/gui.lock";
    QDir().mkpath(QDir::homePath() + "/.wormhole");
    QLockFile lockFile(lockPath);
    if (!lockFile.tryLock(100)) {
        QMessageBox::information(nullptr, QStringLiteral("Wormhole"),
            QStringLiteral("Wormhole is already running."));
        return 1;
    }

    // Load icon from resources
    QIcon appIcon(QStringLiteral(":/wormhole.png"));
    if (!appIcon.isNull())
        app.setWindowIcon(appIcon);

    MainWindow window;
    window.show();

    return app.exec();
}
