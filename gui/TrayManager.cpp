#include "TrayManager.h"
#include "DaemonClient.h"
#include "utils.h"

#include <QMainWindow>
#include <QApplication>
#include <QMessageBox>

extern "C" {
#include "ipc_client.h"
}

TrayManager::TrayManager(QMainWindow *mainWindow, DaemonClient *client,
                         QObject *parent)
    : QObject(parent), m_mainWindow(mainWindow), m_client(client)
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QApplication::windowIcon());
    m_trayIcon->setToolTip(QStringLiteral("Wormhole - Disconnected"));

    // Build tray menu
    m_trayMenu = new QMenu;

    m_showAction = m_trayMenu->addAction(tr("Show/Hide"), this, [this]() {
        if (m_mainWindow->isVisible())
            m_mainWindow->hide();
        else {
            m_mainWindow->show();
            m_mainWindow->raise();
            m_mainWindow->activateWindow();
        }
    });

    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("Send File..."), this, &TrayManager::sendFileRequested);
    m_trayMenu->addAction(tr("Receive File..."), this, &TrayManager::receiveFileRequested);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("Quit"), this, [this]() {
        auto result = QMessageBox::question(m_mainWindow, tr("Quit Wormhole"),
            tr("Are you sure you want to quit Wormhole?"),
            QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::Yes)
            QApplication::quit();
    });

    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &TrayManager::onTrayActivated);

    m_trayIcon->show();
}

TrayManager::~TrayManager()
{
    delete m_trayMenu;
}

bool TrayManager::isAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayManager::updateTooltip(uint32_t peers, uint64_t storage, bool relay)
{
    QString storageStr = WormholeGui::formatSize(storage);

    QString tooltip = QStringLiteral("Wormhole - %1 | %2 peers | %3")
        .arg(relay ? tr("Connected") : tr("No relay"),
             QString::number(peers), storageStr);
    m_trayIcon->setToolTip(tooltip);
}

void TrayManager::setConnected(bool connected)
{
    m_connected = connected;
    if (!connected)
        m_trayIcon->setToolTip(QStringLiteral("Wormhole - Disconnected"));
}

void TrayManager::onEventReceived(uint8_t type, uint32_t opId, QByteArray payload)
{
    Q_UNUSED(opId);
    const uint8_t *data = reinterpret_cast<const uint8_t *>(payload.constData());
    uint32_t size = static_cast<uint32_t>(payload.size());

    if (type == IPC_EVENT_TRANSFER && size >= 6) {
        uint8_t direction = data[4];
        uint8_t state = data[5];

        if (state == TRANSFER_STATE_COMPLETED) {
            QString msg = (direction == TRANSFER_DIR_SEND)
                ? tr("File sent successfully.")
                : tr("File received successfully.");
            showNotification(tr("Transfer Complete"), msg);
        } else if (state == TRANSFER_STATE_FAILED) {
            QString msg = (direction == TRANSFER_DIR_SEND)
                ? tr("File send failed.")
                : tr("File receive failed.");
            showNotification(tr("Transfer Failed"), msg,
                             QSystemTrayIcon::Warning);
        }
    }
    else if (type == IPC_EVENT_HEALTH && size >= 1) {
        uint8_t issueCount = data[0];
        if (issueCount > 0) {
            QString msg = tr("Health check: %1 chunk(s) need attention.").arg(issueCount);
            showNotification(tr("Health Alert"), msg, QSystemTrayIcon::Warning);
        }
    }
}

void TrayManager::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger ||
        reason == QSystemTrayIcon::DoubleClick) {
        if (m_mainWindow->isVisible()) {
            m_mainWindow->hide();
        } else {
            m_mainWindow->show();
            m_mainWindow->raise();
            m_mainWindow->activateWindow();
        }
    }
}

void TrayManager::showNotification(const QString &title, const QString &message,
                                    QSystemTrayIcon::MessageIcon icon)
{
    if (m_trayIcon->supportsMessages())
        m_trayIcon->showMessage(title, message, icon, 5000);
}
