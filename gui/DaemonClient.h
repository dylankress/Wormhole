#pragma once

#include <QObject>
#include <QThread>
#include <QByteArray>
#include "IpcWorker.h"

class DaemonClient : public QObject
{
    Q_OBJECT

public:
    explicit DaemonClient(QObject *parent = nullptr);
    ~DaemonClient();

    void start();
    void stop();
    bool isConnected() const;
    void setSocketName(const QString &name);

    // Thread-safe command forwarding (queued connections to worker)
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
    // Connection state
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

    // Transfer
    void transferStarted(uint32_t transferId);
    void transferFailed(QString errorMsg);
    void transferListReceived(QByteArray data);

    // Files
    void fileListReceived(QByteArray data);
    void fileStoreStarted(QString filename);
    void fileStored(bool ok, QString error);
    void fileRetrieveStarted(QString filename);
    void fileRetrieved(bool ok, QString error);
    void fileDeleted(bool ok, QString error);
    void keyExported(bool ok, QByteArray key, QString error);
    void keyImported(bool ok, QString error);

    // Config
    void configListReceived(QByteArray data);
    void configSetResult(bool ok, bool restartRequired, QString error);

    // Peers
    void peerListReceived(QByteArray data);

private:
    QThread m_workerThread;
    IpcWorker *m_worker = nullptr;
};
