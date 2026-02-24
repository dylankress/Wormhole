#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>

class QMainWindow;
class DaemonClient;

class TrayManager : public QObject
{
    Q_OBJECT

public:
    explicit TrayManager(QMainWindow *mainWindow, DaemonClient *client,
                         QObject *parent = nullptr);
    ~TrayManager();

    bool isAvailable() const;
    void updateTooltip(uint32_t peers, uint64_t storage, bool relay);
    void setConnected(bool connected);

public slots:
    void onEventReceived(uint8_t type, uint32_t opId, QByteArray payload);

signals:
    void sendFileRequested();
    void receiveFileRequested();

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void showNotification(const QString &title, const QString &message,
                          QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);

    QMainWindow *m_mainWindow;
    DaemonClient *m_client;
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QAction *m_showAction;
    bool m_connected = false;
};
