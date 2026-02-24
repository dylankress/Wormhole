#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QTabWidget>

class DaemonClient;
class TransferWidget;
class FileListWidget;
class PeerListWidget;
class TrayManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onReconnecting();
    void onStatusUpdated(uint32_t peers, uint32_t chunks, uint64_t storage,
                         bool relay, bool listener);
    void onAbout();
    void onSettings();
    void tryStartDaemon();

private:
    void setupMenuBar();
    void setupStatusBar();
    void setupCentralWidget();
    DaemonClient *m_client;

    // Central
    QTabWidget *m_tabWidget;
    TransferWidget *m_transferWidget;
    FileListWidget *m_fileListWidget;
    PeerListWidget *m_peerListWidget;

    // Status bar
    QLabel *m_connectionLabel;
    QLabel *m_peersLabel;
    QLabel *m_storageLabel;
    QLabel *m_relayLabel;

    // System tray
    TrayManager *m_trayManager;

    bool m_hasEverConnected = false;
    bool m_daemonStartOffered = false;
};
