#pragma once

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <atomic>

extern "C" {
#include "ipc_client.h"
}

class IpcWorker : public QObject
{
    Q_OBJECT

public:
    explicit IpcWorker(QObject *parent = nullptr);
    ~IpcWorker();

    bool isConnected() const { return m_connected.load(); }
    void setSocketName(const QString &name) { m_socketName = name; }

public slots:
    void start();
    void stop();

    // Command slots (invoked from main thread via queued connections)
    void sendFile(const QString &path);
    void receiveFile(const QString &ticket, const QString &outputDir);
    void cancelTransfer(uint32_t opId);
    void listTransfers();
    void listFiles();
    void storeFile(const QString &path);
    void retrieveFile(const QByteArray &hash, const QString &outputPath);
    void deleteFile(const QByteArray &hash);
    void exportKey(const QByteArray &hash);
    void importKey(const QByteArray &hash, const QByteArray &key);
    void listConfig();
    void setConfig(const QString &key, const QString &value);
    void listPeers();
    void getDhtStatus();

signals:
    void connected();
    void disconnected();
    void reconnecting();

    // Status
    void statusUpdated(uint32_t peers, uint32_t chunks, uint64_t storage,
                       bool relay, bool listener);
    void dhtStatusUpdated(uint32_t nodes, uint32_t values,
                          uint64_t msgsSent, uint64_t msgsRecv);

    // Events
    void eventReceived(uint8_t type, uint32_t opId, QByteArray payload);

    // Transfer responses
    void transferStarted(uint32_t transferId);
    void transferFailed(QString errorMsg);
    void transferListReceived(QByteArray data);

    // File responses
    void fileListReceived(QByteArray data);
    void fileStoreStarted(QString filename);
    void fileStored(bool ok, QString error);
    void fileRetrieveStarted(QString filename);
    void fileRetrieved(bool ok, QString error);
    void fileDeleted(bool ok, QString error);
    void keyExported(bool ok, QByteArray key, QString error);
    void keyImported(bool ok, QString error);

    // Config responses
    void configListReceived(QByteArray data);
    void configSetResult(bool ok, bool restartRequired, QString error);

    // Peer responses
    void peerListReceived(QByteArray data);

private slots:
    void tryConnect();
    void pollStatus();
    void processEvents();

private:
    void doDisconnect();
    uint32_t nextOpId();

    IPC_CLIENT *m_client = nullptr;
    std::atomic<bool> m_connected{false};
    bool m_running = false;

    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_statusTimer = nullptr;
    QTimer *m_eventTimer = nullptr;

    int m_reconnectDelay = 1000;
    static constexpr int MaxReconnectDelay = 30000;

    uint32_t m_nextOpId = 1;
    uint8_t *m_responseBuf = nullptr;
    QString m_socketName;  // empty = default (wormhole_4567)
};
