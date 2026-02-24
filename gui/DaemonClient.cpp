#include "DaemonClient.h"

DaemonClient::DaemonClient(QObject *parent)
    : QObject(parent)
{
    m_worker = new IpcWorker();
    m_worker->moveToThread(&m_workerThread);

    // Forward all worker signals
    connect(m_worker, &IpcWorker::connected, this, &DaemonClient::connected);
    connect(m_worker, &IpcWorker::disconnected, this, &DaemonClient::disconnected);
    connect(m_worker, &IpcWorker::reconnecting, this, &DaemonClient::reconnecting);
    connect(m_worker, &IpcWorker::statusUpdated, this, &DaemonClient::statusUpdated);
    connect(m_worker, &IpcWorker::dhtStatusUpdated, this, &DaemonClient::dhtStatusUpdated);
    connect(m_worker, &IpcWorker::eventReceived, this, &DaemonClient::eventReceived);
    connect(m_worker, &IpcWorker::transferStarted, this, &DaemonClient::transferStarted);
    connect(m_worker, &IpcWorker::transferFailed, this, &DaemonClient::transferFailed);
    connect(m_worker, &IpcWorker::transferListReceived, this, &DaemonClient::transferListReceived);
    connect(m_worker, &IpcWorker::fileListReceived, this, &DaemonClient::fileListReceived);
    connect(m_worker, &IpcWorker::fileStoreStarted, this, &DaemonClient::fileStoreStarted);
    connect(m_worker, &IpcWorker::fileStored, this, &DaemonClient::fileStored);
    connect(m_worker, &IpcWorker::fileRetrieveStarted, this, &DaemonClient::fileRetrieveStarted);
    connect(m_worker, &IpcWorker::fileRetrieved, this, &DaemonClient::fileRetrieved);
    connect(m_worker, &IpcWorker::fileDeleted, this, &DaemonClient::fileDeleted);
    connect(m_worker, &IpcWorker::keyExported, this, &DaemonClient::keyExported);
    connect(m_worker, &IpcWorker::keyImported, this, &DaemonClient::keyImported);
    connect(m_worker, &IpcWorker::configListReceived, this, &DaemonClient::configListReceived);
    connect(m_worker, &IpcWorker::configSetResult, this, &DaemonClient::configSetResult);
    connect(m_worker, &IpcWorker::peerListReceived, this, &DaemonClient::peerListReceived);

    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
}

DaemonClient::~DaemonClient()
{
    stop();
}

void DaemonClient::start()
{
    m_workerThread.start();
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);
}

void DaemonClient::stop()
{
    if (m_workerThread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
        m_workerThread.quit();
        m_workerThread.wait(5000);
    }
}

bool DaemonClient::isConnected() const
{
    return m_worker && m_worker->isConnected();
}

void DaemonClient::sendFile(const QString &path)
{
    QMetaObject::invokeMethod(m_worker, "sendFile", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void DaemonClient::receiveFile(const QString &ticket, const QString &outputDir)
{
    QMetaObject::invokeMethod(m_worker, "receiveFile", Qt::QueuedConnection,
                              Q_ARG(QString, ticket), Q_ARG(QString, outputDir));
}

void DaemonClient::cancelTransfer(uint32_t opId)
{
    QMetaObject::invokeMethod(m_worker, "cancelTransfer", Qt::QueuedConnection,
                              Q_ARG(uint32_t, opId));
}

void DaemonClient::listTransfers()
{
    QMetaObject::invokeMethod(m_worker, "listTransfers", Qt::QueuedConnection);
}

void DaemonClient::listFiles()
{
    QMetaObject::invokeMethod(m_worker, "listFiles", Qt::QueuedConnection);
}

void DaemonClient::storeFile(const QString &path)
{
    QMetaObject::invokeMethod(m_worker, "storeFile", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void DaemonClient::retrieveFile(const QByteArray &hash, const QString &outputPath)
{
    QMetaObject::invokeMethod(m_worker, "retrieveFile", Qt::QueuedConnection,
                              Q_ARG(QByteArray, hash), Q_ARG(QString, outputPath));
}

void DaemonClient::deleteFile(const QByteArray &hash)
{
    QMetaObject::invokeMethod(m_worker, "deleteFile", Qt::QueuedConnection,
                              Q_ARG(QByteArray, hash));
}

void DaemonClient::exportKey(const QByteArray &hash)
{
    QMetaObject::invokeMethod(m_worker, "exportKey", Qt::QueuedConnection,
                              Q_ARG(QByteArray, hash));
}

void DaemonClient::importKey(const QByteArray &hash, const QByteArray &key)
{
    QMetaObject::invokeMethod(m_worker, "importKey", Qt::QueuedConnection,
                              Q_ARG(QByteArray, hash), Q_ARG(QByteArray, key));
}

void DaemonClient::listConfig()
{
    QMetaObject::invokeMethod(m_worker, "listConfig", Qt::QueuedConnection);
}

void DaemonClient::setConfig(const QString &key, const QString &value)
{
    QMetaObject::invokeMethod(m_worker, "setConfig", Qt::QueuedConnection,
                              Q_ARG(QString, key), Q_ARG(QString, value));
}

void DaemonClient::listPeers()
{
    QMetaObject::invokeMethod(m_worker, "listPeers", Qt::QueuedConnection);
}

void DaemonClient::getDhtStatus()
{
    QMetaObject::invokeMethod(m_worker, "getDhtStatus", Qt::QueuedConnection);
}
