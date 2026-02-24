#include "IpcWorker.h"
#include <QThread>

IpcWorker::IpcWorker(QObject *parent)
    : QObject(parent)
{
    m_responseBuf = new uint8_t[IPC_MAX_MESSAGE_SIZE];
}

IpcWorker::~IpcWorker()
{
    stop();
    delete[] m_responseBuf;
}

void IpcWorker::start()
{
    m_running = true;

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &IpcWorker::tryConnect);

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &IpcWorker::pollStatus);

    m_eventTimer = new QTimer(this);
    connect(m_eventTimer, &QTimer::timeout, this, &IpcWorker::processEvents);

    tryConnect();
}

void IpcWorker::stop()
{
    m_running = false;

    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
        delete m_reconnectTimer;
        m_reconnectTimer = nullptr;
    }
    if (m_statusTimer) {
        m_statusTimer->stop();
        delete m_statusTimer;
        m_statusTimer = nullptr;
    }
    if (m_eventTimer) {
        m_eventTimer->stop();
        delete m_eventTimer;
        m_eventTimer = nullptr;
    }

    doDisconnect();
}

void IpcWorker::tryConnect()
{
    if (!m_running) return;

    m_client = IpcClient_Connect();
    if (!m_client) {
        emit reconnecting();
        m_reconnectTimer->start(m_reconnectDelay);
        m_reconnectDelay = qMin(m_reconnectDelay * 2, MaxReconnectDelay);
        return;
    }

    // Subscribe to all events
    if (!IpcClient_Subscribe(m_client, IPC_EVENT_MASK_ALL)) {
        IpcClient_Disconnect(m_client);
        m_client = nullptr;
        emit reconnecting();
        m_reconnectTimer->start(m_reconnectDelay);
        m_reconnectDelay = qMin(m_reconnectDelay * 2, MaxReconnectDelay);
        return;
    }

    m_connected.store(true);
    m_reconnectDelay = 1000;
    emit connected();

    m_statusTimer->start(5000);
    m_eventTimer->start(100);

    // Immediate first status poll
    pollStatus();
}

void IpcWorker::pollStatus()
{
    if (!m_client || !m_connected.load()) return;

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_STATUS, nextOpId(),
        nullptr, 0, m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (!ok || respSize < 19 || m_responseBuf[0] != IPC_STATUS_OK) {
        doDisconnect();
        return;
    }

    uint32_t peers   = ReadUint32LE(m_responseBuf + 1);
    uint32_t chunks  = ReadUint32LE(m_responseBuf + 5);
    uint64_t storage = ReadUint64LE(m_responseBuf + 9);
    bool relay       = m_responseBuf[17] != 0;
    bool listener    = m_responseBuf[18] != 0;

    emit statusUpdated(peers, chunks, storage, relay, listener);
}

void IpcWorker::processEvents()
{
    if (!m_client || !m_connected.load()) return;

    // Drain all available events (non-blocking)
    for (int i = 0; i < 50; i++) {
        uint8_t eventType = 0;
        uint32_t opId = 0;
        uint8_t payload[4096];
        uint32_t payloadSize = 0;

        BOOLEAN got = IpcClient_ReadEvent(
            m_client, &eventType, &opId,
            payload, sizeof(payload), &payloadSize, 0);

        if (!got) break;

        QByteArray payloadData(reinterpret_cast<const char *>(payload),
                               static_cast<int>(payloadSize));
        emit eventReceived(eventType, opId, payloadData);
    }
}

void IpcWorker::doDisconnect()
{
    if (m_statusTimer) m_statusTimer->stop();
    if (m_eventTimer) m_eventTimer->stop();

    if (m_client) {
        IpcClient_Disconnect(m_client);
        m_client = nullptr;
    }

    bool wasConnected = m_connected.exchange(false);
    if (wasConnected)
        emit disconnected();

    if (m_running && m_reconnectTimer) {
        m_reconnectTimer->start(m_reconnectDelay);
        m_reconnectDelay = qMin(m_reconnectDelay * 2, MaxReconnectDelay);
    }
}

uint32_t IpcWorker::nextOpId()
{
    return m_nextOpId++;
}

// --- Command implementations ---

void IpcWorker::sendFile(const QString &path)
{
    if (!m_client || !m_connected.load()) {
        emit transferFailed(QStringLiteral("Not connected to daemon"));
        return;
    }

    QByteArray pathBytes = path.toUtf8();
    uint16_t pathLen = static_cast<uint16_t>(pathBytes.size());

    QByteArray payload(2 + pathLen, '\0');
    WriteUint16LE(reinterpret_cast<uint8_t *>(payload.data()), pathLen);
    memcpy(payload.data() + 2, pathBytes.constData(), pathLen);

    uint32_t opId = nextOpId();
    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_SEND, opId,
        reinterpret_cast<const uint8_t *>(payload.constData()),
        static_cast<uint32_t>(payload.size()),
        m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 5 && m_responseBuf[0] == IPC_STATUS_OK) {
        uint32_t transferId = ReadUint32LE(m_responseBuf + 1);
        emit transferStarted(transferId);
    } else {
        char errMsg[IPC_MAX_ERROR_MSG_LEN];
        uint8_t status;
        if (ok && respSize >= 1) {
            IpcReadErrorResponse(m_responseBuf, respSize, &status, errMsg, sizeof(errMsg));
            emit transferFailed(QString::fromUtf8(errMsg));
        } else {
            emit transferFailed(QStringLiteral("Failed to send file: connection error"));
        }
    }
}

void IpcWorker::receiveFile(const QString &ticket, const QString &outputDir)
{
    if (!m_client || !m_connected.load()) {
        emit transferFailed(QStringLiteral("Not connected to daemon"));
        return;
    }

    QByteArray ticketBytes = ticket.toUtf8();
    uint8_t ticketLen = static_cast<uint8_t>(ticketBytes.size());

    QByteArray payload;
    payload.append(static_cast<char>(ticketLen));
    payload.append(ticketBytes);

    if (!outputDir.isEmpty()) {
        QByteArray dirBytes = outputDir.toUtf8();
        uint16_t dirLen = static_cast<uint16_t>(dirBytes.size());
        uint8_t dirLenBuf[2];
        WriteUint16LE(dirLenBuf, dirLen);
        payload.append(reinterpret_cast<const char *>(dirLenBuf), 2);
        payload.append(dirBytes);
    }

    uint32_t opId = nextOpId();
    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_RECEIVE, opId,
        reinterpret_cast<const uint8_t *>(payload.constData()),
        static_cast<uint32_t>(payload.size()),
        m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 5 && m_responseBuf[0] == IPC_STATUS_OK) {
        uint32_t transferId = ReadUint32LE(m_responseBuf + 1);
        emit transferStarted(transferId);
    } else {
        char errMsg[IPC_MAX_ERROR_MSG_LEN];
        uint8_t status;
        if (ok && respSize >= 1) {
            IpcReadErrorResponse(m_responseBuf, respSize, &status, errMsg, sizeof(errMsg));
            emit transferFailed(QString::fromUtf8(errMsg));
        } else {
            emit transferFailed(QStringLiteral("Failed to receive file: connection error"));
        }
    }
}

void IpcWorker::cancelTransfer(uint32_t opId)
{
    if (!m_client || !m_connected.load()) return;
    IpcClient_CancelOp(m_client, opId);
}

void IpcWorker::listTransfers()
{
    if (!m_client || !m_connected.load()) return;

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_TRANSFER_LIST, nextOpId(),
        nullptr, 0, m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize > 0) {
        QByteArray data(reinterpret_cast<const char *>(m_responseBuf),
                        static_cast<int>(respSize));
        emit transferListReceived(data);
    }
}

void IpcWorker::listFiles()
{
    if (!m_client || !m_connected.load()) return;

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_LIST_FILES, nextOpId(),
        nullptr, 0, m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize > 0) {
        QByteArray data(reinterpret_cast<const char *>(m_responseBuf),
                        static_cast<int>(respSize));
        emit fileListReceived(data);
    }
}

void IpcWorker::storeFile(const QString &path)
{
    if (!m_client || !m_connected.load()) return;

    emit fileStoreStarted(path);

    QByteArray pathBytes = path.toUtf8();
    uint16_t pathLen = static_cast<uint16_t>(pathBytes.size());

    QByteArray payload(2 + pathLen, '\0');
    WriteUint16LE(reinterpret_cast<uint8_t *>(payload.data()), pathLen);
    memcpy(payload.data() + 2, pathBytes.constData(), pathLen);

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_STORE, nextOpId(),
        reinterpret_cast<const uint8_t *>(payload.constData()),
        static_cast<uint32_t>(payload.size()),
        m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 1 && m_responseBuf[0] == IPC_STATUS_OK) {
        emit fileStored(true, QString());
    } else {
        char errMsg[IPC_MAX_ERROR_MSG_LEN];
        uint8_t status;
        if (ok && respSize >= 1) {
            IpcReadErrorResponse(m_responseBuf, respSize, &status, errMsg, sizeof(errMsg));
            emit fileStored(false, QString::fromUtf8(errMsg));
        } else {
            emit fileStored(false, QStringLiteral("Connection error"));
        }
    }
}

void IpcWorker::retrieveFile(const QByteArray &hash, const QString &outputPath)
{
    if (!m_client || !m_connected.load()) return;

    emit fileRetrieveStarted(outputPath);

    QByteArray pathBytes = outputPath.toUtf8();
    uint16_t pathLen = static_cast<uint16_t>(pathBytes.size());

    QByteArray payload;
    payload.append(hash.constData(), WH_HASH_SIZE);
    uint8_t lenBuf[2];
    WriteUint16LE(lenBuf, pathLen);
    payload.append(reinterpret_cast<const char *>(lenBuf), 2);
    payload.append(pathBytes);

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_FILE_GET, nextOpId(),
        reinterpret_cast<const uint8_t *>(payload.constData()),
        static_cast<uint32_t>(payload.size()),
        m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 1 && m_responseBuf[0] == IPC_STATUS_OK) {
        emit fileRetrieved(true, QString());
    } else {
        emit fileRetrieved(false, QStringLiteral("Retrieve failed"));
    }
}

void IpcWorker::deleteFile(const QByteArray &hash)
{
    if (!m_client || !m_connected.load()) return;

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_FILE_DELETE, nextOpId(),
        reinterpret_cast<const uint8_t *>(hash.constData()),
        WH_HASH_SIZE,
        m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 1 && m_responseBuf[0] == IPC_STATUS_OK) {
        emit fileDeleted(true, QString());
    } else {
        emit fileDeleted(false, QStringLiteral("Delete failed"));
    }
}

void IpcWorker::exportKey(const QByteArray &hash)
{
    if (!m_client || !m_connected.load()) return;

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_EXPORT_KEY, nextOpId(),
        reinterpret_cast<const uint8_t *>(hash.constData()),
        WH_HASH_SIZE,
        m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 1 && m_responseBuf[0] == IPC_STATUS_OK) {
        // Response: [1B status][32B key]
        if (respSize >= 33) {
            QByteArray key(reinterpret_cast<const char *>(m_responseBuf + 1), 32);
            emit keyExported(true, key, QString());
        } else {
            emit keyExported(false, QByteArray(), QStringLiteral("Invalid response"));
        }
    } else {
        emit keyExported(false, QByteArray(), QStringLiteral("Export failed"));
    }
}

void IpcWorker::importKey(const QByteArray &hash, const QByteArray &key)
{
    if (!m_client || !m_connected.load()) return;

    QByteArray payload;
    payload.append(hash.constData(), WH_HASH_SIZE);
    payload.append(key.constData(), 32);

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_IMPORT_KEY, nextOpId(),
        reinterpret_cast<const uint8_t *>(payload.constData()),
        static_cast<uint32_t>(payload.size()),
        m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 1 && m_responseBuf[0] == IPC_STATUS_OK) {
        emit keyImported(true, QString());
    } else {
        emit keyImported(false, QStringLiteral("Import failed"));
    }
}

void IpcWorker::listConfig()
{
    if (!m_client || !m_connected.load()) return;

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_CONFIG_LIST, nextOpId(),
        nullptr, 0, m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize > 0) {
        QByteArray data(reinterpret_cast<const char *>(m_responseBuf),
                        static_cast<int>(respSize));
        emit configListReceived(data);
    }
}

void IpcWorker::setConfig(const QString &key, const QString &value)
{
    if (!m_client || !m_connected.load()) return;

    QByteArray keyBytes = key.toUtf8();
    QByteArray valBytes = value.toUtf8();
    uint8_t keyLen = static_cast<uint8_t>(keyBytes.size());
    uint8_t valLen = static_cast<uint8_t>(valBytes.size());

    QByteArray payload;
    payload.append(static_cast<char>(keyLen));
    payload.append(keyBytes);
    payload.append(static_cast<char>(valLen));
    payload.append(valBytes);

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_CONFIG_SET, nextOpId(),
        reinterpret_cast<const uint8_t *>(payload.constData()),
        static_cast<uint32_t>(payload.size()),
        m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 2 && m_responseBuf[0] == IPC_STATUS_OK) {
        bool restartRequired = m_responseBuf[1] != 0;
        emit configSetResult(true, restartRequired, QString());
    } else {
        char errMsg[IPC_MAX_ERROR_MSG_LEN];
        uint8_t status;
        if (ok && respSize >= 1) {
            IpcReadErrorResponse(m_responseBuf, respSize, &status, errMsg, sizeof(errMsg));
            emit configSetResult(false, false, QString::fromUtf8(errMsg));
        } else {
            emit configSetResult(false, false, QStringLiteral("Connection error"));
        }
    }
}

void IpcWorker::listPeers()
{
    if (!m_client || !m_connected.load()) return;

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_PEER_LIST, nextOpId(),
        nullptr, 0, m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize > 0) {
        QByteArray data(reinterpret_cast<const char *>(m_responseBuf),
                        static_cast<int>(respSize));
        emit peerListReceived(data);
    }
}

void IpcWorker::getDhtStatus()
{
    if (!m_client || !m_connected.load()) return;

    uint32_t respSize = 0;
    BOOLEAN ok = IpcClient_SendCommandV2(
        m_client, IPC_CMD_DHT_STATUS, nextOpId(),
        nullptr, 0, m_responseBuf, IPC_MAX_MESSAGE_SIZE, &respSize);

    if (ok && respSize >= 25 && m_responseBuf[0] == IPC_STATUS_OK) {
        uint32_t nodes  = ReadUint32LE(m_responseBuf + 1);
        uint32_t values = ReadUint32LE(m_responseBuf + 5);
        uint64_t sent   = ReadUint64LE(m_responseBuf + 9);
        uint64_t recv   = ReadUint64LE(m_responseBuf + 17);
        emit dhtStatusUpdated(nodes, values, sent, recv);
    }
}
