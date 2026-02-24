#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

class DaemonClient;

class PeerListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PeerListWidget(DaemonClient *client, QWidget *parent = nullptr);

public slots:
    void onPeerListReceived(QByteArray data);
    void onDhtStatusUpdated(uint32_t nodes, uint32_t values,
                            uint64_t msgsSent, uint64_t msgsRecv);
    void onEventReceived(uint8_t type, uint32_t opId, QByteArray payload);
    void onConnected();
    void onDisconnected();
    void refresh();

private:
    DaemonClient *m_client;
    QTableWidget *m_peerTable;
    QLabel *m_emptyLabel;
    QPushButton *m_refreshButton;
    QLabel *m_dhtNodesLabel;
    QLabel *m_dhtValuesLabel;
    QLabel *m_dhtSentLabel;
    QLabel *m_dhtRecvLabel;
    QTimer *m_refreshTimer;

    void updateEmptyState();
};
