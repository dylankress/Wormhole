#include "PeerListWidget.h"
#include "DaemonClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDateTime>
#include <QShortcut>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

extern "C" {
#include "ipc_client.h"
}

PeerListWidget::PeerListWidget(DaemonClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    auto *mainLayout = new QVBoxLayout(this);

    // DHT status summary
    auto *statusGroup = new QGroupBox(tr("Network Status"));
    auto *statusLayout = new QHBoxLayout(statusGroup);

    m_dhtNodesLabel  = new QLabel(tr("DHT Nodes: -"));
    m_dhtValuesLabel = new QLabel(tr("DHT Values: -"));
    m_dhtSentLabel   = new QLabel(tr("Msgs Sent: -"));
    m_dhtRecvLabel   = new QLabel(tr("Msgs Recv: -"));

    statusLayout->addWidget(m_dhtNodesLabel);
    statusLayout->addWidget(m_dhtValuesLabel);
    statusLayout->addWidget(m_dhtSentLabel);
    statusLayout->addWidget(m_dhtRecvLabel);
    statusLayout->addStretch();

    // Toolbar with refresh button
    auto *toolbar = new QHBoxLayout;
    m_refreshButton = new QPushButton(tr("Refresh"));
    connect(m_refreshButton, &QPushButton::clicked, this, &PeerListWidget::refresh);
    toolbar->addWidget(m_refreshButton);
    toolbar->addStretch();

    // Empty state placeholder
    m_emptyLabel = new QLabel(tr("No peers discovered yet.\nThe DHT will find peers automatically."));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 14px; padding: 40px;"));
    m_emptyLabel->setWordWrap(true);

    // Peer table
    m_peerTable = new QTableWidget(0, 4);
    m_peerTable->setHorizontalHeaderLabels(
        {tr("Node ID"), tr("Address"), tr("Port"), tr("Last Seen")});
    m_peerTable->horizontalHeader()->setStretchLastSection(true);
    m_peerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_peerTable->verticalHeader()->setVisible(false);
    m_peerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_peerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    mainLayout->addWidget(statusGroup);
    mainLayout->addLayout(toolbar);
    mainLayout->addWidget(m_emptyLabel);
    mainLayout->addWidget(m_peerTable, 1);

    updateEmptyState();

    // Keyboard shortcuts
    new QShortcut(QKeySequence::Refresh, this, SLOT(refresh()));  // F5

    // Periodic refresh
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &PeerListWidget::refresh);

    // Pause timer when disconnected
    connect(m_client, &DaemonClient::connected, this, &PeerListWidget::onConnected);
    connect(m_client, &DaemonClient::disconnected, this, &PeerListWidget::onDisconnected);

    if (m_client->isConnected())
        m_refreshTimer->start(30000);
}

void PeerListWidget::refresh()
{
    m_client->listPeers();
    m_client->getDhtStatus();
}

void PeerListWidget::onConnected()
{
    m_refreshButton->setEnabled(true);
    m_refreshTimer->start(30000);
    refresh();
}

void PeerListWidget::onDisconnected()
{
    m_refreshTimer->stop();
    m_refreshButton->setEnabled(false);

    // Clear stale data
    m_peerTable->setRowCount(0);
    updateEmptyState();

    // Reset DHT status labels
    m_dhtNodesLabel->setText(tr("DHT Nodes: -"));
    m_dhtValuesLabel->setText(tr("DHT Values: -"));
    m_dhtSentLabel->setText(tr("Msgs Sent: -"));
    m_dhtRecvLabel->setText(tr("Msgs Recv: -"));
}

void PeerListWidget::updateEmptyState()
{
    bool empty = m_peerTable->rowCount() == 0;
    m_emptyLabel->setVisible(empty);
    m_peerTable->setVisible(!empty);
}

void PeerListWidget::onPeerListReceived(QByteArray data)
{
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(data.constData());
    uint32_t size = static_cast<uint32_t>(data.size());

    if (size < 5 || buf[0] != IPC_STATUS_OK) return;

    uint32_t count = ReadUint32LE(buf + 1);
    m_peerTable->setRowCount(0);

    uint32_t off = 5;
    uint32_t perPeerSize = 32 + 1 + 16 + 2 + 8; // 59 bytes

    for (uint32_t i = 0; i < count; i++) {
        if (off + perPeerSize > size) break;

        // Node ID — full 64-char hex for tooltip, first 8 chars + "..." for display
        uint32_t nodeIdOffset = off;
        QString nodeId;
        for (int h = 0; h < 4; h++)
            nodeId += QStringLiteral("%1").arg(buf[off + h], 2, 16, QLatin1Char('0'));
        nodeId += QStringLiteral("...");

        QString fullNodeId;
        for (int h = 0; h < 32; h++)
            fullNodeId += QStringLiteral("%1").arg(buf[nodeIdOffset + h], 2, 16, QLatin1Char('0'));
        off += 32;

        uint8_t addrType = buf[off]; off += 1;

        // Address
        QString address;
        if (addrType == 4) {
            // IPv4
            address = QStringLiteral("%1.%2.%3.%4")
                .arg(buf[off]).arg(buf[off+1]).arg(buf[off+2]).arg(buf[off+3]);
        } else if (addrType == 6) {
            // Format IPv6 address from 16 raw bytes
            char addrStr[INET6_ADDRSTRLEN];
            struct in6_addr addr6;
            memcpy(&addr6, buf + off, 16);
            const char *result = inet_ntop(AF_INET6, &addr6, addrStr, sizeof(addrStr));
            if (result)
                address = QStringLiteral("[%1]").arg(QString::fromLatin1(addrStr));
            else
                address = QStringLiteral("?");
        } else {
            address = QStringLiteral("?");
        }
        off += 16;

        uint16_t port = ReadUint16LE(buf + off); off += 2;
        uint64_t lastSeen = ReadUint64LE(buf + off); off += 8;

        QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(lastSeen));

        // Relative time for Last Seen
        qint64 secsAgo = dt.secsTo(QDateTime::currentDateTime());
        QString relativeTime;
        if (secsAgo < 0)
            relativeTime = QStringLiteral("just now");
        else if (secsAgo < 60)
            relativeTime = QStringLiteral("%1s ago").arg(secsAgo);
        else if (secsAgo < 3600)
            relativeTime = QStringLiteral("%1m ago").arg(secsAgo / 60);
        else if (secsAgo < 86400)
            relativeTime = QStringLiteral("%1h ago").arg(secsAgo / 3600);
        else
            relativeTime = QStringLiteral("%1d ago").arg(secsAgo / 86400);

        int row = m_peerTable->rowCount();
        m_peerTable->insertRow(row);

        auto *nodeIdItem = new QTableWidgetItem(nodeId);
        nodeIdItem->setToolTip(fullNodeId);
        m_peerTable->setItem(row, 0, nodeIdItem);
        m_peerTable->setItem(row, 1, new QTableWidgetItem(address));
        m_peerTable->setItem(row, 2, new QTableWidgetItem(QString::number(port)));

        auto *lastSeenItem = new QTableWidgetItem(relativeTime);
        lastSeenItem->setToolTip(dt.toString(Qt::ISODate));
        m_peerTable->setItem(row, 3, lastSeenItem);
    }

    updateEmptyState();
}

void PeerListWidget::onDhtStatusUpdated(uint32_t nodes, uint32_t values,
                                         uint64_t msgsSent, uint64_t msgsRecv)
{
    m_dhtNodesLabel->setText(tr("DHT Nodes: %1").arg(nodes));
    m_dhtValuesLabel->setText(tr("DHT Values: %1").arg(values));
    m_dhtSentLabel->setText(tr("Msgs Sent: %1").arg(msgsSent));
    m_dhtRecvLabel->setText(tr("Msgs Recv: %1").arg(msgsRecv));
}

void PeerListWidget::onEventReceived(uint8_t type, uint32_t opId, QByteArray payload)
{
    Q_UNUSED(opId);
    Q_UNUSED(payload);
    if (type == IPC_EVENT_PEER_CHANGE) {
        refresh();
    }
}
