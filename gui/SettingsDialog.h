#pragma once

#include <QDialog>
#include <QMap>
#include <QWidget>

class QFormLayout;
class QLabel;
class DaemonClient;

struct ConfigEntry {
    QString key;
    QString value;
    bool hotReloadable = false;
    QWidget *widget = nullptr;
};

class QPushButton;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(DaemonClient *client, QWidget *parent = nullptr);

public slots:
    void onConfigListReceived(QByteArray data);

private slots:
    void onApply();

private:
    QWidget *createWidget(const QString &key, const QString &value);
    QString readWidget(const QString &key, QWidget *widget);

    DaemonClient *m_client;
    QFormLayout *m_formLayout;
    QLabel *m_statusLabel;
    QPushButton *m_restartBtn = nullptr;
    QMap<QString, ConfigEntry> m_entries;
    QMap<QString, QString> m_originalValues;
};
