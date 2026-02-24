#include "FileListWidget.h"
#include "DaemonClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QShortcut>

#include "utils.h"

extern "C" {
#include "ipc_client.h"
}

FileListWidget::FileListWidget(DaemonClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setAcceptDrops(true);

    auto *mainLayout = new QVBoxLayout(this);

    // Search filter
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("Search files..."));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &FileListWidget::onSearchChanged);

    // Toolbar
    auto *toolbar = new QHBoxLayout;
    m_storeBtn = new QPushButton(tr("Store File"));
    m_retrieveBtn = new QPushButton(tr("Retrieve"));
    m_deleteBtn = new QPushButton(tr("Delete"));
    m_exportKeyBtn = new QPushButton(tr("Export Key"));
    m_importKeyBtn = new QPushButton(tr("Import Key"));
    m_refreshBtn = new QPushButton(tr("Refresh"));

    m_retrieveBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
    m_exportKeyBtn->setEnabled(false);

    toolbar->addWidget(m_storeBtn);
    toolbar->addWidget(m_retrieveBtn);
    toolbar->addWidget(m_deleteBtn);
    toolbar->addWidget(m_exportKeyBtn);
    toolbar->addWidget(m_importKeyBtn);
    toolbar->addWidget(m_refreshBtn);
    toolbar->addStretch();

    connect(m_refreshBtn, &QPushButton::clicked, this, &FileListWidget::refresh);

    connect(m_storeBtn, &QPushButton::clicked, this, &FileListWidget::onStoreFile);
    connect(m_retrieveBtn, &QPushButton::clicked, this, &FileListWidget::onRetrieveFile);
    connect(m_deleteBtn, &QPushButton::clicked, this, &FileListWidget::onDeleteFile);
    connect(m_exportKeyBtn, &QPushButton::clicked, this, &FileListWidget::onExportKey);
    connect(m_importKeyBtn, &QPushButton::clicked, this, &FileListWidget::onImportKey);

    // File tree
    m_fileTree = new QTreeWidget;
    m_fileTree->setHeaderLabels(
        {tr("Name"), tr("Size"), tr("Status"), tr("Chunks"), tr("Copied"),
         tr("Stored"), tr("File ID")});
    m_fileTree->setRootIsDecorated(false);
    m_fileTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileTree->header()->setStretchLastSection(true);
    m_fileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileTree->setSortingEnabled(true);
    m_fileTree->sortByColumn(5, Qt::DescendingOrder);

    connect(m_fileTree, &QTreeWidget::customContextMenuRequested,
            this, &FileListWidget::onContextMenu);
    connect(m_fileTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        bool hasSelection = !m_fileTree->selectedItems().isEmpty();
        m_retrieveBtn->setEnabled(hasSelection);
        m_deleteBtn->setEnabled(hasSelection);
        m_exportKeyBtn->setEnabled(hasSelection);
    });

    // Empty state label
    m_emptyLabel = new QLabel(tr("No stored files.\nDrag files here or click Store File to get started."));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: gray; font-size: 14px; padding: 40px;");
    m_emptyLabel->setWordWrap(true);

    // Progress indicator for store/retrieve operations
    auto *progressLayout = new QHBoxLayout;
    m_progressLabel = new QLabel;
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 0); // indeterminate
    m_progressBar->setMaximumHeight(16);
    progressLayout->addWidget(m_progressLabel);
    progressLayout->addWidget(m_progressBar, 1);
    m_progressLabel->setVisible(false);
    m_progressBar->setVisible(false);

    mainLayout->addLayout(toolbar);
    mainLayout->addWidget(m_searchEdit);
    mainLayout->addLayout(progressLayout);
    mainLayout->addWidget(m_emptyLabel);
    mainLayout->addWidget(m_fileTree, 1);

    // Connect signals
    connect(m_client, &DaemonClient::fileStoreStarted, this, &FileListWidget::onFileStoreStarted);
    connect(m_client, &DaemonClient::fileStored, this, &FileListWidget::onFileStored);
    connect(m_client, &DaemonClient::fileRetrieveStarted, this, &FileListWidget::onFileRetrieveStarted);
    connect(m_client, &DaemonClient::fileRetrieved, this, &FileListWidget::onFileRetrieved);
    connect(m_client, &DaemonClient::fileDeleted, this, &FileListWidget::onFileDeleted);
    connect(m_client, &DaemonClient::keyExported, this, &FileListWidget::onKeyExported);

    // Periodic refresh
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &FileListWidget::refresh);
    // Timer will be started by connected signal (don't start before connection)

    // Keyboard shortcuts
    new QShortcut(QKeySequence::Refresh, this, SLOT(refresh()));         // F5
    new QShortcut(QKeySequence::Delete, this, SLOT(onDeleteFile()));     // Delete

    // Timer control: pause when disconnected, resume when connected
    connect(m_client, &DaemonClient::connected, this, [this]() {
        m_refreshTimer->start(30000);
    });
    connect(m_client, &DaemonClient::disconnected, this, [this]() {
        m_refreshTimer->stop();
    });

    updateEmptyState();
}

void FileListWidget::refresh()
{
    m_client->listFiles();
}

void FileListWidget::onFileListReceived(QByteArray data)
{
    parseFileList(data);
}

void FileListWidget::parseFileList(const QByteArray &data)
{
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(data.constData());
    uint32_t size = static_cast<uint32_t>(data.size());

    if (size < 5 || buf[0] != IPC_STATUS_OK) return;

    uint32_t count = ReadUint32LE(buf + 1);
    m_fileTree->clear();

    uint32_t off = 5;
    for (uint32_t i = 0; i < count; i++) {
        if (off + WH_HASH_SIZE + 1 + 8 + 4 + 4 + 8 + 2 > size) break;

        QByteArray hash(reinterpret_cast<const char *>(buf + off), WH_HASH_SIZE);
        off += WH_HASH_SIZE;

        uint8_t status = buf[off]; off += 1;
        uint64_t fileSize = ReadUint64LE(buf + off); off += 8;
        uint32_t chunkCount = ReadUint32LE(buf + off); off += 4;
        uint32_t replCount = ReadUint32LE(buf + off); off += 4;
        uint64_t storeTime = ReadUint64LE(buf + off); off += 8;
        uint16_t nameLen = ReadUint16LE(buf + off); off += 2;

        QString filename;
        if (nameLen > 0 && off + nameLen <= size) {
            filename = QString::fromUtf8(reinterpret_cast<const char *>(buf + off), nameLen);
        }
        off += nameLen;

        // Build hex ID (first 8 chars)
        QString hexId;
        for (int h = 0; h < 4 && h < WH_HASH_SIZE; h++)
            hexId += QStringLiteral("%1").arg(static_cast<uint8_t>(hash[h]), 2, 16, QLatin1Char('0'));

        QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(storeTime));

        auto *item = new QTreeWidgetItem;
        item->setText(0, filename);
        item->setText(1, WormholeGui::formatSize(fileSize));
        item->setText(2, statusString(status));
        item->setText(3, QString::number(chunkCount));
        item->setText(4, QStringLiteral("%1/%2").arg(replCount).arg(chunkCount));
        item->setText(5, dt.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
        item->setText(6, hexId);
        item->setData(0, Qt::UserRole, hash);

        m_fileTree->addTopLevelItem(item);
    }

    updateEmptyState();

    // Reapply search filter after refresh
    if (!m_searchEdit->text().isEmpty())
        onSearchChanged(m_searchEdit->text());
}

void FileListWidget::onEventReceived(uint8_t type, uint32_t opId, QByteArray payload)
{
    Q_UNUSED(opId);
    Q_UNUSED(payload);
    if (type == IPC_EVENT_FILE_STATUS) {
        refresh();
    }
}

void FileListWidget::onStoreFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select File to Store"));
    if (path.isEmpty()) return;
    m_client->storeFile(path);
}

void FileListWidget::onRetrieveFile()
{
    QByteArray hash = selectedHash();
    if (hash.isEmpty()) return;

    QString outputPath = QFileDialog::getSaveFileName(this, tr("Save Retrieved File"));
    if (outputPath.isEmpty()) return;

    m_client->retrieveFile(hash, outputPath);
}

void FileListWidget::onDeleteFile()
{
    QByteArray hash = selectedHash();
    if (hash.isEmpty()) return;

    auto items = m_fileTree->selectedItems();
    QString name = items.isEmpty() ? tr("this file") : items.first()->text(0);
    QString size = items.isEmpty() ? QString() : items.first()->text(1);
    QString status = items.isEmpty() ? QString() : items.first()->text(2);

    QString msg = tr("Delete \"%1\"").arg(name);
    if (!size.isEmpty())
        msg += tr(" (%1, %2)").arg(size, status);
    msg += tr(" and all its chunks?");

    auto result = QMessageBox::question(this, tr("Delete File"), msg,
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes)
        m_client->deleteFile(hash);
}

void FileListWidget::onExportKey()
{
    QByteArray hash = selectedHash();
    if (hash.isEmpty()) return;
    m_client->exportKey(hash);
}

void FileListWidget::onImportKey()
{
    bool ok;
    QString hexKey = QInputDialog::getText(this, tr("Import Key"),
        tr("File ID (hex prefix):"), QLineEdit::Normal, QString(), &ok);
    if (!ok || hexKey.isEmpty()) return;

    QString keyHex = QInputDialog::getText(this, tr("Import Key"),
        tr("Encryption key (hex):"), QLineEdit::Normal, QString(), &ok);
    if (!ok || keyHex.isEmpty()) return;

    // Validate hex input
    auto isValidHex = [](const QString &s) {
        for (QChar c : s) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        }
        return s.length() > 0 && s.length() % 2 == 0;
    };

    if (!isValidHex(hexKey)) {
        QMessageBox::warning(this, tr("Invalid Input"),
            tr("File ID must be an even-length hex string (0-9, a-f)."));
        return;
    }

    if (!isValidHex(keyHex) || keyHex.length() != 64) {
        QMessageBox::warning(this, tr("Invalid Input"),
            tr("Encryption key must be exactly 64 hex characters (32 bytes)."));
        return;
    }

    // Parse hex ID to hash
    QByteArray hash = QByteArray::fromHex(hexKey.toUtf8());
    if (hash.size() < WH_HASH_SIZE)
        hash.append(QByteArray(WH_HASH_SIZE - hash.size(), '\0'));
    hash.truncate(WH_HASH_SIZE);

    QByteArray key = QByteArray::fromHex(keyHex.toUtf8());

    m_client->importKey(hash, key);
}

void FileListWidget::onContextMenu(const QPoint &pos)
{
    auto *item = m_fileTree->itemAt(pos);
    if (!item) return;

    // Select the right-clicked item so actions operate on it
    m_fileTree->setCurrentItem(item);

    QString fileId = item->text(6);

    QMenu menu;
    menu.addAction(tr("Retrieve"), this, &FileListWidget::onRetrieveFile);
    menu.addAction(tr("Delete"), this, &FileListWidget::onDeleteFile);
    menu.addSeparator();
    menu.addAction(tr("Export Key"), this, &FileListWidget::onExportKey);
    menu.addAction(tr("Copy File ID"), this, [this, fileId]() {
        QApplication::clipboard()->setText(fileId);
    });

    menu.exec(m_fileTree->mapToGlobal(pos));
}

void FileListWidget::onFileStoreStarted(const QString &filename)
{
    QFileInfo fi(filename);
    m_progressLabel->setText(tr("Storing %1...").arg(fi.fileName()));
    m_progressLabel->setVisible(true);
    m_progressBar->setVisible(true);
    m_storeBtn->setEnabled(false);
}

void FileListWidget::onFileStored(bool ok, QString error)
{
    m_progressLabel->setVisible(false);
    m_progressBar->setVisible(false);
    m_storeBtn->setEnabled(true);

    if (ok) {
        refresh();
    } else {
        QMessageBox::warning(this, tr("Store Failed"), error);
    }
}

void FileListWidget::onFileRetrieveStarted(const QString &filename)
{
    QFileInfo fi(filename);
    m_progressLabel->setText(tr("Retrieving to %1...").arg(fi.fileName()));
    m_progressLabel->setVisible(true);
    m_progressBar->setVisible(true);
    m_retrieveBtn->setEnabled(false);
}

void FileListWidget::onFileRetrieved(bool ok, QString error)
{
    m_progressLabel->setVisible(false);
    m_progressBar->setVisible(false);
    m_retrieveBtn->setEnabled(true);

    if (ok) {
        QMessageBox::information(this, tr("File Retrieved"),
                                 tr("File retrieved successfully."));
    } else {
        QMessageBox::warning(this, tr("Retrieve Failed"), error);
    }
}

void FileListWidget::onFileDeleted(bool ok, QString error)
{
    if (ok) {
        refresh();
    } else {
        QMessageBox::warning(this, tr("Delete Failed"), error);
    }
}

void FileListWidget::onKeyExported(bool ok, QByteArray key, QString error)
{
    if (ok) {
        QString hexKey = key.toHex();
        QApplication::clipboard()->setText(hexKey);
        QMessageBox::information(this, tr("Key Exported"),
            tr("Encryption key copied to clipboard:\n%1").arg(hexKey));
    } else {
        QMessageBox::warning(this, tr("Export Failed"), error);
    }
}

void FileListWidget::onSearchChanged(const QString &text)
{
    for (int i = 0; i < m_fileTree->topLevelItemCount(); i++) {
        auto *item = m_fileTree->topLevelItem(i);
        bool match = text.isEmpty() ||
                     item->text(0).contains(text, Qt::CaseInsensitive) ||
                     item->text(6).contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void FileListWidget::updateEmptyState()
{
    bool empty = m_fileTree->topLevelItemCount() == 0;
    m_emptyLabel->setVisible(empty);
    m_fileTree->setVisible(!empty);
}

void FileListWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void FileListWidget::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (!path.isEmpty())
            m_client->storeFile(path);
    }
}

QByteArray FileListWidget::selectedHash()
{
    auto items = m_fileTree->selectedItems();
    if (items.isEmpty()) return {};
    return items.first()->data(0, Qt::UserRole).toByteArray();
}

QString FileListWidget::statusString(uint8_t status)
{
    switch (status) {
    case FILE_STATUS_STORING:     return tr("Storing");
    case FILE_STATUS_REPLICATING: return tr("Replicating");
    case FILE_STATUS_SAFE:        return tr("Safe");
    case FILE_STATUS_OFFLOADED:   return tr("Offloaded");
    default:                      return tr("Unknown");
    }
}

