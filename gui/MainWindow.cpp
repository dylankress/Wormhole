#include "MainWindow.h"
#include "DaemonClient.h"
#include "TransferWidget.h"
#include "FileListWidget.h"
#include "PeerListWidget.h"
#include "SettingsDialog.h"
#include "TrayManager.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QCloseEvent>
#include <QMessageBox>
#include <QApplication>
#include <QFileDialog>
#include <QProcess>
#include <QSettings>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>

#include "utils.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Wormhole"));
    setMinimumSize(800, 500);

    QSettings settings;
    restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
    restoreState(settings.value("mainwindow/windowState").toByteArray());

    m_client = new DaemonClient(this);

    setupMenuBar();
    setupCentralWidget();
    setupStatusBar();

    // System tray
    m_trayManager = new TrayManager(this, m_client, this);

    // Connect daemon client signals
    connect(m_client, &DaemonClient::connected, this, &MainWindow::onConnected);
    connect(m_client, &DaemonClient::disconnected, this, &MainWindow::onDisconnected);
    connect(m_client, &DaemonClient::reconnecting, this, &MainWindow::onReconnecting);
    connect(m_client, &DaemonClient::statusUpdated, this, &MainWindow::onStatusUpdated);

    // Route events to widgets
    connect(m_client, &DaemonClient::eventReceived,
            m_transferWidget, &TransferWidget::onEventReceived);
    connect(m_client, &DaemonClient::eventReceived,
            m_fileListWidget, &FileListWidget::onEventReceived);
    connect(m_client, &DaemonClient::eventReceived,
            m_peerListWidget, &PeerListWidget::onEventReceived);
    connect(m_client, &DaemonClient::eventReceived,
            m_trayManager, &TrayManager::onEventReceived);

    // Transfer signals
    connect(m_client, &DaemonClient::transferStarted,
            m_transferWidget, &TransferWidget::onTransferStarted);
    connect(m_client, &DaemonClient::transferFailed, this, [this](QString error) {
        QMessageBox::warning(this, tr("Transfer Error"), error);
    });

    // File list signal
    connect(m_client, &DaemonClient::fileListReceived,
            m_fileListWidget, &FileListWidget::onFileListReceived);

    // Peer list signals
    connect(m_client, &DaemonClient::peerListReceived,
            m_peerListWidget, &PeerListWidget::onPeerListReceived);
    connect(m_client, &DaemonClient::dhtStatusUpdated,
            m_peerListWidget, &PeerListWidget::onDhtStatusUpdated);

    // Status update for tray tooltip
    connect(m_client, &DaemonClient::statusUpdated, this,
            [this](uint32_t peers, uint32_t, uint64_t storage, bool relay, bool) {
        m_trayManager->updateTooltip(peers, storage, relay);
    });

    // Tray actions
    connect(m_trayManager, &TrayManager::sendFileRequested, this, [this]() {
        show();
        raise();
        m_tabWidget->setCurrentWidget(m_transferWidget);
    });
    connect(m_trayManager, &TrayManager::receiveFileRequested, this, [this]() {
        show();
        raise();
        m_tabWidget->setCurrentWidget(m_transferWidget);
    });

    // Start daemon connection
    m_client->start();
}

MainWindow::~MainWindow()
{
    if (!m_stopped) {
        m_stopped = true;
        m_client->stop();
    }
}

void MainWindow::setupMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("Start &Daemon"), this, &MainWindow::tryStartDaemon);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit,
                        qApp, &QApplication::quit);

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Settings..."), this, &MainWindow::onSettings);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::onAbout);
}

void MainWindow::setupCentralWidget()
{
    m_tabWidget = new QTabWidget;

    m_transferWidget = new TransferWidget(m_client);
    m_fileListWidget = new FileListWidget(m_client);
    m_peerListWidget = new PeerListWidget(m_client);

    m_tabWidget->addTab(m_transferWidget, tr("Transfers"));
    m_tabWidget->addTab(m_fileListWidget, tr("Files"));
    m_tabWidget->addTab(m_peerListWidget, tr("Network"));

    setCentralWidget(m_tabWidget);
}

void MainWindow::setupStatusBar()
{
    m_connectionLabel = new QLabel(tr("Disconnected"));
    m_peersLabel = new QLabel(tr("Peers: -"));
    m_storageLabel = new QLabel(tr("Storage: -"));
    m_relayLabel = new QLabel(tr("Relay: -"));

    statusBar()->addWidget(m_connectionLabel);
    statusBar()->addPermanentWidget(m_peersLabel);
    statusBar()->addPermanentWidget(m_storageLabel);
    statusBar()->addPermanentWidget(m_relayLabel);
}

void MainWindow::onConnected()
{
    m_hasEverConnected = true;
    m_connectionLabel->setText(QStringLiteral("<span style='color:green;'>&#x25CF;</span> Connected"));
    m_trayManager->setConnected(true);

    // Refresh all tabs
    m_fileListWidget->refresh();
    m_peerListWidget->refresh();
    m_transferWidget->refreshTransfers();
    m_client->listTransfers();
}

void MainWindow::onDisconnected()
{
    m_connectionLabel->setText(QStringLiteral("<span style='color:red;'>&#x25CF;</span> Disconnected"));
    m_peersLabel->clear();
    m_storageLabel->clear();
    m_relayLabel->clear();
    m_trayManager->setConnected(false);

    if (!m_hasEverConnected && !m_daemonStartOffered) {
        m_daemonStartOffered = true;
        QTimer::singleShot(2000, this, [this]() {
            if (m_hasEverConnected) return;
            auto result = QMessageBox::question(this, tr("Daemon Not Running"),
                tr("The Wormhole daemon is not running. Would you like to start it?"),
                QMessageBox::Yes | QMessageBox::No);
            if (result == QMessageBox::Yes) {
                tryStartDaemon();
            }
        });
    }
}

void MainWindow::onReconnecting()
{
    m_connectionLabel->setText(QStringLiteral("<span style='color:orange;'>&#x25CF;</span> Reconnecting..."));

    // Offer to start daemon if we've never connected
    if (!m_hasEverConnected && !m_daemonStartOffered) {
        m_daemonStartOffered = true;
        QTimer::singleShot(2000, this, [this]() {
            if (m_hasEverConnected) return;
            auto result = QMessageBox::question(this, tr("Daemon Not Running"),
                tr("The Wormhole daemon is not running. Would you like to start it?"),
                QMessageBox::Yes | QMessageBox::No);
            if (result == QMessageBox::Yes) {
                tryStartDaemon();
            }
        });
    }
}

void MainWindow::onStatusUpdated(uint32_t peers, uint32_t chunks, uint64_t storage,
                                  bool relay, bool listener)
{
    Q_UNUSED(chunks);
    Q_UNUSED(listener);
    m_peersLabel->setText(tr("Peers: %1").arg(peers));
    m_storageLabel->setText(tr("Storage: %1").arg(WormholeGui::formatSize(storage)));
    m_relayLabel->setText(relay ? tr("Relay: connected") : tr("Relay: disconnected"));
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About Wormhole"),
        tr("<h3>Wormhole</h3>"
           "<p>Decentralized P2P file storage platform.</p>"
           "<p>Send, receive, and store files without centralized cloud providers.</p>"
           "<p>Files are erasure-coded and replicated across multiple peers for durability.</p>"));
}

void MainWindow::onSettings()
{
    auto *dialog = new SettingsDialog(m_client, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::tryStartDaemon()
{
    QString daemonPath;
    QString appDir = QCoreApplication::applicationDirPath();

#ifdef _WIN32
    daemonPath = appDir + "/wormholed.exe";
#else
    daemonPath = appDir + "/wormholed";
#endif

    if (!QFile::exists(daemonPath))
        daemonPath = QStringLiteral("wormholed");

    if (!QProcess::startDetached(daemonPath, QStringList(), appDir)) {
        QMessageBox::warning(this, tr("Daemon Start Failed"),
            tr("Could not start the daemon.\n"
               "Make sure '%1' exists and is executable.").arg(daemonPath));
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue("mainwindow/geometry", saveGeometry());
    settings.setValue("mainwindow/windowState", saveState());

    if (m_trayManager->isAvailable()) {
        hide();
        event->ignore();
    } else {
        if (!m_stopped) {
            m_stopped = true;
            m_client->stop();
        }
        event->accept();
        QApplication::quit();
    }
}

