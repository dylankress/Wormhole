#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMap>

class DaemonClient;

struct TransferInfo {
    uint32_t transferId = 0;
    uint32_t opId = 0;
    QString filename;
    int direction = 0; // 0=send, 1=receive
    int state = 0;
    uint64_t bytesDone = 0;
    uint64_t bytesTotal = 0;
    uint32_t speed = 0;
    uint32_t eta = 0;
    QString ticket;
    QString error;
};

class TransferWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TransferWidget(DaemonClient *client, QWidget *parent = nullptr);

public slots:
    void onTransferStarted(uint32_t transferId);
    void onEventReceived(uint8_t type, uint32_t opId, QByteArray payload);
    void refreshTransfers();

private slots:
    void onSendFile();
    void onSendDirectory();
    void onReceive();
    void onCancelTransfer();
    void onCopyTicket();
    void onClearCompleted();

private:
    void updateTransferRow(uint32_t transferId);
    void updateEmptyState();
    int findRow(uint32_t transferId);
    QString stateString(int state);
    QString formatSpeed(uint32_t bps);
    QString formatEta(uint32_t seconds);

    DaemonClient *m_client;

    // Send panel
    QPushButton *m_sendFileBtn;
    QPushButton *m_sendDirBtn;
    QLabel *m_ticketLabel;
    QPushButton *m_copyTicketBtn;

    // Receive panel
    QLineEdit *m_ticketInput;
    QPushButton *m_receiveBtn;

    // Transfer list
    QTableWidget *m_transferTable;
    QLabel *m_emptyLabel;
    QPushButton *m_clearBtn;

    QMap<uint32_t, TransferInfo> m_transfers;
};
