#include "TransferWidget.h"
#include "DaemonClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>
#include <QProgressBar>
#include <QStackedWidget>
#include <QShortcut>

#include "utils.h"

extern "C" {
#include "ipc_client.h"
}

TransferWidget::TransferWidget(DaemonClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Send panel ---
    auto *sendGroup = new QGroupBox(tr("Send"));
    auto *sendLayout = new QHBoxLayout(sendGroup);

    m_sendFileBtn = new QPushButton(tr("Send File"));
    m_sendDirBtn = new QPushButton(tr("Send Directory"));
    m_ticketLabel = new QLabel;
    m_ticketLabel->setFont(QFont("Courier", 14, QFont::Bold));
    m_ticketLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_copyTicketBtn = new QPushButton(tr("Copy"));
    m_copyTicketBtn->setEnabled(false);
    m_copyTicketBtn->setFixedWidth(60);

    sendLayout->addWidget(m_sendFileBtn);
    sendLayout->addWidget(m_sendDirBtn);
    sendLayout->addWidget(m_ticketLabel, 1);
    sendLayout->addWidget(m_copyTicketBtn);

    connect(m_sendFileBtn, &QPushButton::clicked, this, &TransferWidget::onSendFile);
    connect(m_sendDirBtn, &QPushButton::clicked, this, &TransferWidget::onSendDirectory);
    connect(m_copyTicketBtn, &QPushButton::clicked, this, &TransferWidget::onCopyTicket);

    // --- Receive panel ---
    auto *recvGroup = new QGroupBox(tr("Receive"));
    auto *recvLayout = new QHBoxLayout(recvGroup);

    m_ticketInput = new QLineEdit;
    m_ticketInput->setPlaceholderText(tr("Enter ticket code (e.g., 3-guitar-battery)"));
    m_receiveBtn = new QPushButton(tr("Receive"));

    recvLayout->addWidget(m_ticketInput, 1);
    recvLayout->addWidget(m_receiveBtn);

    connect(m_receiveBtn, &QPushButton::clicked, this, &TransferWidget::onReceive);
    connect(m_ticketInput, &QLineEdit::returnPressed, this, &TransferWidget::onReceive);

    // --- Transfer list ---
    auto *listGroup = new QGroupBox(tr("Transfers"));
    auto *listLayout = new QVBoxLayout(listGroup);

    m_transferTable = new QTableWidget(0, 7);
    m_transferTable->setHorizontalHeaderLabels(
        {tr("File"), tr("Direction"), tr("Progress"), tr("Speed"),
         tr("ETA"), tr("Status"), tr("Action")});
    m_transferTable->horizontalHeader()->setStretchLastSection(true);
    m_transferTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_transferTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_transferTable->setColumnWidth(2, 200);
    m_transferTable->verticalHeader()->setVisible(false);
    m_transferTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_transferTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_emptyLabel = new QLabel(tr("No transfers yet. Send or receive a file to get started."));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: gray; font-size: 14px; padding: 40px;");

    auto *stack = new QStackedWidget;
    stack->addWidget(m_transferTable);
    stack->addWidget(m_emptyLabel);
    stack->setCurrentWidget(m_emptyLabel);

    auto *btnLayout = new QHBoxLayout;
    m_clearBtn = new QPushButton(tr("Clear Completed"));
    connect(m_clearBtn, &QPushButton::clicked, this, &TransferWidget::onClearCompleted);
    btnLayout->addStretch();
    btnLayout->addWidget(m_clearBtn);

    listLayout->addWidget(stack, 1);
    listLayout->addLayout(btnLayout);

    mainLayout->addWidget(sendGroup);
    mainLayout->addWidget(recvGroup);
    mainLayout->addWidget(listGroup, 1);

    // Keyboard shortcuts
    new QShortcut(QKeySequence(tr("Ctrl+S")), this, SLOT(onSendFile()));
    new QShortcut(QKeySequence(tr("Ctrl+R")), this, SLOT(onReceive()));
}

void TransferWidget::onSendFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select File to Send"));
    if (path.isEmpty()) return;
    m_client->sendFile(path);
}

void TransferWidget::onSendDirectory()
{
    QString path = QFileDialog::getExistingDirectory(this, tr("Select Directory to Send"));
    if (path.isEmpty()) return;
    m_client->sendFile(path);
}

void TransferWidget::onReceive()
{
    QString ticket = m_ticketInput->text().trimmed();
    if (ticket.isEmpty()) return;

    QString outputDir = QFileDialog::getExistingDirectory(
        this, tr("Save to Directory"),
        QDir::homePath() + QStringLiteral("/Downloads"));
    if (outputDir.isEmpty()) return;

    m_client->receiveFile(ticket, outputDir);
    m_ticketInput->clear();
}

void TransferWidget::onCancelTransfer()
{
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;
    uint32_t id = btn->property("transferId").toUInt();
    m_client->cancelTransfer(id);
}

void TransferWidget::onCopyTicket()
{
    QString ticket = m_ticketLabel->text();
    if (!ticket.isEmpty())
        QApplication::clipboard()->setText(ticket);
}

void TransferWidget::onClearCompleted()
{
    QList<uint32_t> toRemove;
    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if (it->state == TRANSFER_STATE_COMPLETED ||
            it->state == TRANSFER_STATE_FAILED ||
            it->state == TRANSFER_STATE_CANCELLED) {
            toRemove.append(it.key());
        }
    }

    for (uint32_t id : toRemove) {
        int row = findRow(id);
        if (row >= 0)
            m_transferTable->removeRow(row);
        m_transfers.remove(id);
    }

    updateEmptyState();
}

void TransferWidget::onTransferStarted(uint32_t transferId)
{
    TransferInfo info;
    info.transferId = transferId;
    info.opId = transferId;
    info.state = TRANSFER_STATE_REGISTERING;
    info.filename = tr("Preparing...");
    m_transfers[transferId] = info;

    int row = m_transferTable->rowCount();
    m_transferTable->insertRow(row);
    m_transferTable->setItem(row, 0, new QTableWidgetItem(info.filename));
    m_transferTable->item(row, 0)->setData(Qt::UserRole, transferId);
    m_transferTable->setItem(row, 1, new QTableWidgetItem(tr("...")));

    auto *progressBar = new QProgressBar;
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    m_transferTable->setCellWidget(row, 2, progressBar);

    m_transferTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("-")));
    m_transferTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("-")));
    m_transferTable->setItem(row, 5, new QTableWidgetItem(stateString(info.state)));

    auto *cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setProperty("transferId", transferId);
    connect(cancelBtn, &QPushButton::clicked, this, &TransferWidget::onCancelTransfer);
    m_transferTable->setCellWidget(row, 6, cancelBtn);

    updateEmptyState();
}

void TransferWidget::onEventReceived(uint8_t type, uint32_t opId, QByteArray payload)
{
    const uint8_t *data = reinterpret_cast<const uint8_t *>(payload.constData());
    uint32_t size = static_cast<uint32_t>(payload.size());

    if (type == IPC_EVENT_PROGRESS && size >= IPC_PROGRESS_PAYLOAD_SIZE) {
        uint32_t eventOpId = ReadUint32LE(data);
        uint64_t bytesDone = ReadUint64LE(data + 4);
        uint64_t bytesTotal = ReadUint64LE(data + 12);
        // uint32_t chunksDone = ReadUint32LE(data + 20);
        // uint32_t chunksTotal = ReadUint32LE(data + 24);
        uint32_t speed = ReadUint32LE(data + 28);
        uint32_t eta = ReadUint32LE(data + 32);

        if (m_transfers.contains(eventOpId)) {
            auto &info = m_transfers[eventOpId];
            info.bytesDone = bytesDone;
            info.bytesTotal = bytesTotal;
            info.speed = speed;
            info.eta = eta;
            info.state = TRANSFER_STATE_TRANSFERRING;
            updateTransferRow(eventOpId);
        }
    }
    else if (type == IPC_EVENT_TRANSFER && size >= 25) {
        // Transfer event: [4B transfer_id][1B direction][1B state][1B ipc_status]
        //   [8B bytes_transferred][8B bytes_total][2B error_msg_len][error_msg]
        //   [1B ticket_len][ticket_bytes]
        uint32_t transferId = ReadUint32LE(data);
        uint8_t direction = data[4];
        uint8_t state = data[5];
        // uint8_t ipc_status = data[6]; // available but unused in GUI
        uint64_t bytesDone = ReadUint64LE(data + 7);
        uint64_t bytesTotal = ReadUint64LE(data + 15);
        uint16_t errorMsgLen = ReadUint16LE(data + 23);

        // Parse error message
        QString errorMsg;
        uint32_t errorEnd = 25 + errorMsgLen;
        if (errorMsgLen > 0 && errorEnd <= size) {
            errorMsg = QString::fromUtf8(
                reinterpret_cast<const char *>(data + 25), errorMsgLen);
        }

        // Parse ticket (after error msg)
        QString ticket;
        uint32_t ticketEnd = errorEnd;
        if (errorEnd < size) {
            uint8_t ticketLen = data[errorEnd];
            ticketEnd = errorEnd + 1 + ticketLen;
            if (ticketLen > 0 && ticketEnd <= size) {
                ticket = QString::fromUtf8(
                    reinterpret_cast<const char *>(data + errorEnd + 1), ticketLen);
            }
        }

        // Parse filename (after ticket)
        QString filename;
        if (ticketEnd < size) {
            uint8_t filenameLen = data[ticketEnd];
            if (filenameLen > 0 && ticketEnd + 1 + filenameLen <= size) {
                filename = QString::fromUtf8(
                    reinterpret_cast<const char *>(data + ticketEnd + 1), filenameLen);
            }
        }

        if (!m_transfers.contains(transferId)) {
            TransferInfo newInfo;
            newInfo.transferId = transferId;
            newInfo.opId = transferId;
            m_transfers[transferId] = newInfo;

            int row = m_transferTable->rowCount();
            m_transferTable->insertRow(row);
            m_transferTable->setItem(row, 0, new QTableWidgetItem(tr("...")));
            m_transferTable->item(row, 0)->setData(Qt::UserRole, transferId);
            m_transferTable->setItem(row, 1, new QTableWidgetItem(
                direction == TRANSFER_DIR_SEND ? tr("Send") : tr("Receive")));

            auto *progressBar = new QProgressBar;
            progressBar->setRange(0, 100);
            m_transferTable->setCellWidget(row, 2, progressBar);

            m_transferTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("-")));
            m_transferTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("-")));
            m_transferTable->setItem(row, 5, new QTableWidgetItem(stateString(state)));

            auto *cancelBtn = new QPushButton(tr("Cancel"));
            cancelBtn->setProperty("transferId", transferId);
            connect(cancelBtn, &QPushButton::clicked, this, &TransferWidget::onCancelTransfer);
            m_transferTable->setCellWidget(row, 6, cancelBtn);

            updateEmptyState();
        }

        auto &info = m_transfers[transferId];
        info.direction = direction;
        info.state = state;
        info.bytesDone = bytesDone;
        info.bytesTotal = bytesTotal;

        if (!errorMsg.isEmpty())
            info.error = errorMsg;

        if (!filename.isEmpty())
            info.filename = filename;

        if (!ticket.isEmpty()) {
            info.ticket = ticket;
            if (direction == TRANSFER_DIR_SEND) {
                m_ticketLabel->setText(ticket);
                m_copyTicketBtn->setEnabled(true);
            }
        }

        updateTransferRow(transferId);
    }
}

void TransferWidget::updateTransferRow(uint32_t transferId)
{
    int row = findRow(transferId);
    if (row < 0) return;

    const auto &info = m_transfers[transferId];

    if (!info.filename.isEmpty())
        m_transferTable->item(row, 0)->setText(info.filename);

    m_transferTable->item(row, 1)->setText(
        info.direction == TRANSFER_DIR_SEND ? tr("Send") : tr("Receive"));

    auto *progressBar = qobject_cast<QProgressBar *>(
        m_transferTable->cellWidget(row, 2));
    if (progressBar && info.bytesTotal > 0) {
        int percent = static_cast<int>((info.bytesDone * 100) / info.bytesTotal);
        progressBar->setValue(percent);
        progressBar->setFormat(QStringLiteral("%1 / %2")
            .arg(WormholeGui::formatSize(info.bytesDone), WormholeGui::formatSize(info.bytesTotal)));
    }

    m_transferTable->item(row, 3)->setText(formatSpeed(info.speed));
    m_transferTable->item(row, 4)->setText(formatEta(info.eta));

    QString statusText = stateString(info.state);
    if (info.state == TRANSFER_STATE_FAILED && !info.error.isEmpty())
        statusText = tr("Failed: %1").arg(info.error);
    m_transferTable->item(row, 5)->setText(statusText);
    if (!info.error.isEmpty())
        m_transferTable->item(row, 5)->setToolTip(info.error);

    // Disable cancel button for completed transfers
    if (info.state == TRANSFER_STATE_COMPLETED ||
        info.state == TRANSFER_STATE_FAILED ||
        info.state == TRANSFER_STATE_CANCELLED) {
        auto *btn = qobject_cast<QPushButton *>(m_transferTable->cellWidget(row, 6));
        if (btn) btn->setEnabled(false);
    }
}

int TransferWidget::findRow(uint32_t transferId)
{
    for (int i = 0; i < m_transferTable->rowCount(); i++) {
        auto *item = m_transferTable->item(i, 0);
        if (item && item->data(Qt::UserRole).toUInt() == transferId)
            return i;
    }
    return -1;
}

QString TransferWidget::stateString(int state)
{
    switch (state) {
    case TRANSFER_STATE_IDLE:         return tr("Idle");
    case TRANSFER_STATE_REGISTERING:  return tr("Registering");
    case TRANSFER_STATE_WAITING_PEER: return tr("Waiting for peer");
    case TRANSFER_STATE_CONNECTING:   return tr("Connecting");
    case TRANSFER_STATE_TRANSFERRING: return tr("Transferring");
    case TRANSFER_STATE_COMPLETED:    return tr("Completed");
    case TRANSFER_STATE_FAILED:       return tr("Failed");
    case TRANSFER_STATE_CANCELLED:    return tr("Cancelled");
    default:                          return tr("Unknown");
    }
}

QString TransferWidget::formatSpeed(uint32_t bps)
{
    if (bps == 0) return QStringLiteral("-");
    if (bps < 1024)
        return QStringLiteral("%1 B/s").arg(bps);
    if (bps < 1024 * 1024)
        return QStringLiteral("%1 KB/s").arg(bps / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB/s").arg(bps / (1024.0 * 1024.0), 0, 'f', 1);
}

QString TransferWidget::formatEta(uint32_t seconds)
{
    if (seconds == 0) return QStringLiteral("-");
    if (seconds < 60) return QStringLiteral("%1s").arg(seconds);
    if (seconds < 3600) return QStringLiteral("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    return QStringLiteral("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

void TransferWidget::updateEmptyState()
{
    auto *stack = qobject_cast<QStackedWidget *>(m_transferTable->parentWidget());
    if (!stack) return;
    stack->setCurrentWidget(m_transferTable->rowCount() == 0
                            ? static_cast<QWidget *>(m_emptyLabel)
                            : static_cast<QWidget *>(m_transferTable));
}

void TransferWidget::refreshTransfers()
{
    m_client->listTransfers();
}
