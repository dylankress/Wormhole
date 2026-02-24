#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <QProgressBar>

class DaemonClient;

struct FileEntry {
    QByteArray hash;
    QString filename;
    uint64_t fileSize = 0;
    uint32_t chunkCount = 0;
    uint32_t replicatedCount = 0;
    uint64_t storeTime = 0;
    uint8_t status = 0;
};

class FileListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FileListWidget(DaemonClient *client, QWidget *parent = nullptr);

public slots:
    void onFileListReceived(QByteArray data);
    void onEventReceived(uint8_t type, uint32_t opId, QByteArray payload);
    void refresh();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onStoreFile();
    void onRetrieveFile();
    void onDeleteFile();
    void onExportKey();
    void onImportKey();
    void onContextMenu(const QPoint &pos);
    void onFileStored(bool ok, QString error);
    void onFileRetrieved(bool ok, QString error);
    void onFileDeleted(bool ok, QString error);
    void onKeyExported(bool ok, QByteArray key, QString error);
    void onSearchChanged(const QString &text);
    void onFileStoreStarted(const QString &filename);
    void onFileRetrieveStarted(const QString &filename);
    void updateEmptyState();

private:
    QByteArray selectedHash();
    QString statusString(uint8_t status);
    void parseFileList(const QByteArray &data);

    DaemonClient *m_client;
    QTreeWidget *m_fileTree;
    QPushButton *m_storeBtn;
    QPushButton *m_retrieveBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_exportKeyBtn;
    QPushButton *m_importKeyBtn;
    QPushButton *m_refreshBtn;
    QLabel *m_emptyLabel;
    QLineEdit *m_searchEdit;
    QTimer *m_refreshTimer;
    QProgressBar *m_progressBar;
    QLabel *m_progressLabel;
};
