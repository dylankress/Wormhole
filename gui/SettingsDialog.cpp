#include "SettingsDialog.h"
#include "DaemonClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QProcess>
#include <QCoreApplication>

extern "C" {
#include "ipc_client.h"
}

static QString settingDescription(const QString &key) {
    static const QMap<QString, QString> descriptions = {
        {"relay_host", "Hostname of the relay server for NAT traversal"},
        {"relay_port", "UDP port of the relay server"},
        {"max_storage_gb", "Maximum disk space (GB) allocated for chunk storage"},
        {"replication_target", "Number of copies to maintain per chunk across the network"},
        {"dht_enabled", "Enable Kademlia DHT for decentralized peer discovery"},
        {"dht_port", "UDP port for DHT communication"},
        {"dht_bootstrap_nodes", "Comma-separated host:port pairs for DHT bootstrap (e.g., node1.example.com:4568,node2.example.com:4568)"},
        {"ec_enabled", "Enable Reed-Solomon erasure coding for data durability"},
        {"ec_data_shards", "Number of data shards per erasure coding stripe"},
        {"ec_parity_shards", "Number of parity shards per erasure coding stripe (must be <= data shards)"},
        {"health_check_interval_sec", "Seconds between chunk health checks (lower = more responsive, higher = less overhead)"},
        {"min_storage_ratio", "Minimum reciprocity ratio (percent) required to accept storage from a peer"},
        {"proof_cache_count", "Number of pre-computed proof-of-storage proofs cached per chunk"},
        {"auto_evict_enabled", "Automatically remove local chunk copies after they are replicated to other peers"}
    };
    return descriptions.value(key, QString());
}

// Keys that use checkboxes
static bool isBoolKey(const QString &key) {
    return key == QStringLiteral("dht_enabled") ||
           key == QStringLiteral("ec_enabled") ||
           key == QStringLiteral("auto_evict_enabled");
}

// Keys that use spinboxes with specific ranges
struct SpinConfig { int min; int max; QString suffix; };
static SpinConfig spinConfig(const QString &key) {
    if (key == QStringLiteral("relay_port"))            return {1, 65535, {}};
    if (key == QStringLiteral("max_storage_gb"))        return {1, 1048576, QStringLiteral(" GB")};
    if (key == QStringLiteral("replication_target"))    return {1, 16, {}};
    if (key == QStringLiteral("dht_port"))              return {1024, 65535, {}};
    if (key == QStringLiteral("ec_data_shards"))        return {2, 128, {}};
    if (key == QStringLiteral("ec_parity_shards"))      return {1, 64, {}};
    if (key == QStringLiteral("health_check_interval_sec")) return {60, 86400, QStringLiteral(" sec")};
    if (key == QStringLiteral("min_storage_ratio"))     return {0, 100, QStringLiteral("%")};
    if (key == QStringLiteral("proof_cache_count"))     return {1, 256, {}};
    return {-1, -1, {}};
}

SettingsDialog::SettingsDialog(DaemonClient *client, QWidget *parent)
    : QDialog(parent), m_client(client)
{
    setWindowTitle(tr("Settings"));
    setMinimumSize(500, 400);

    auto *mainLayout = new QVBoxLayout(this);

    // Scrollable form
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    auto *scrollWidget = new QWidget;
    m_formLayout = new QFormLayout(scrollWidget);
    scrollArea->setWidget(scrollWidget);

    m_statusLabel = new QLabel;
    m_statusLabel->setVisible(false);

    m_restartBtn = new QPushButton(tr("Restart Daemon"));
    m_restartBtn->setVisible(false);
    connect(m_restartBtn, &QPushButton::clicked, this, [this]() {
        QString appDir = QCoreApplication::applicationDirPath();
        QString wormholePath = appDir + "/wormhole";
#ifdef _WIN32
        wormholePath += ".exe";
#endif
        QProcess::startDetached(wormholePath, {"daemon", "restart"});
        m_restartBtn->setVisible(false);
        m_statusLabel->setText(tr("Daemon restarting..."));
    });

    auto *buttonBox = new QDialogButtonBox;
    auto *applyBtn = buttonBox->addButton(QDialogButtonBox::Apply);
    buttonBox->addButton(QDialogButtonBox::Close);

    connect(applyBtn, &QPushButton::clicked, this, &SettingsDialog::onApply);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(scrollArea, 1);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_restartBtn);
    mainLayout->addWidget(buttonBox);

    connect(m_client, &DaemonClient::configListReceived,
            this, &SettingsDialog::onConfigListReceived);
    connect(m_client, &DaemonClient::configSetResult, this,
            [this](bool ok, bool restartRequired, QString error) {
        if (ok) {
            QString msg = tr("Settings saved.");
            if (restartRequired) {
                msg += tr(" Some changes require a daemon restart.");
                m_restartBtn->setVisible(true);
            } else {
                m_restartBtn->setVisible(false);
            }
            m_statusLabel->setText(msg);
            m_statusLabel->setStyleSheet(
                QStringLiteral("color: %1; padding: 4px;")
                    .arg(restartRequired ? "orange" : "green"));
        } else {
            m_statusLabel->setText(tr("Error: %1").arg(error));
            m_statusLabel->setStyleSheet(QStringLiteral("color: red; padding: 4px;"));
        }
        m_statusLabel->setVisible(true);
    });

    // Request config from daemon
    m_client->listConfig();
}

void SettingsDialog::onConfigListReceived(QByteArray data)
{
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(data.constData());
    uint32_t size = static_cast<uint32_t>(data.size());

    if (size < 5 || buf[0] != IPC_STATUS_OK) return;

    uint32_t count = ReadUint32LE(buf + 1);

    // Clear existing
    while (m_formLayout->rowCount() > 0)
        m_formLayout->removeRow(0);
    m_entries.clear();
    m_originalValues.clear();

    uint32_t off = 5;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 1 > size) break;
        uint8_t keyLen = buf[off]; off += 1;
        if (off + keyLen + 1 > size) break;
        QString key = QString::fromUtf8(reinterpret_cast<const char *>(buf + off), keyLen);
        off += keyLen;

        uint8_t valLen = buf[off]; off += 1;
        if (off + valLen + 1 > size) break;
        QString value = QString::fromUtf8(reinterpret_cast<const char *>(buf + off), valLen);
        off += valLen;

        bool hotReload = buf[off] != 0; off += 1;

        ConfigEntry entry;
        entry.key = key;
        entry.value = value;
        entry.hotReloadable = hotReload;
        entry.widget = createWidget(key, value);

        QString label = key;
        if (!hotReload)
            label += tr(" (restart required)");

        m_formLayout->addRow(label, entry.widget);
        m_entries[key] = entry;
        m_originalValues[key] = value;
    }
}

void SettingsDialog::onApply()
{
    m_statusLabel->setVisible(false);

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        QString newValue = readWidget(it.key(), it->widget);
        if (newValue != m_originalValues[it.key()]) {
            m_client->setConfig(it.key(), newValue);
            m_originalValues[it.key()] = newValue;
        }
    }
}

QWidget *SettingsDialog::createWidget(const QString &key, const QString &value)
{
    QWidget *widget = nullptr;

    if (isBoolKey(key)) {
        auto *cb = new QCheckBox;
        cb->setChecked(value.toInt() != 0);
        widget = cb;
    } else {
        SpinConfig sc = spinConfig(key);
        if (sc.min >= 0) {
            auto *spin = new QSpinBox;
            spin->setRange(sc.min, sc.max);
            spin->setValue(value.toInt());
            if (!sc.suffix.isEmpty())
                spin->setSuffix(sc.suffix);
            widget = spin;
        } else {
            widget = new QLineEdit(value);
        }
    }

    QString desc = settingDescription(key);
    if (!desc.isEmpty())
        widget->setToolTip(desc);

    return widget;
}

QString SettingsDialog::readWidget(const QString &key, QWidget *widget)
{
    if (isBoolKey(key)) {
        auto *cb = qobject_cast<QCheckBox *>(widget);
        return cb && cb->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
    }

    if (auto *spin = qobject_cast<QSpinBox *>(widget))
        return QString::number(spin->value());

    if (auto *edit = qobject_cast<QLineEdit *>(widget))
        return edit->text();

    return {};
}
