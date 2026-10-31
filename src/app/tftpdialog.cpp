#include "tftpdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "tftpserver.h"

using namespace DialogKit;

TftpDialog::TftpDialog(TftpServer *server, QWidget *parent) : QDialog(parent), m_server(server)
{
    setWindowTitle(tr("TFTP Server"));
    resize(640, 520);
    QSettings st;
    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("TFTP Server"), tr("For devices that fetch images and configurations")));

    m_folder = new QLineEdit(st.value("tftp/folder", QDir::homePath() + "/tftp").toString());
    auto *browse = secondaryButton(tr("Browse..."));
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);
    m_address = new QComboBox;
    m_address->addItem(tr("All addresses"), QHostAddress(QHostAddress::AnyIPv4).toString());
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol)
            m_address->addItem(a.toString(), a.toString());
    const int saved = m_address->findData(st.value("tftp/address").toString());
    if (saved >= 0)
        m_address->setCurrentIndex(saved);
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(st.value("tftp/port", 69).toInt());
    m_uploads = new QCheckBox(tr("Let devices upload files to this folder"));
    m_uploads->setChecked(st.value("tftp/uploads", false).toBool());
    m_overwrite = new QCheckBox(tr("Let uploads replace existing files"));
    m_overwrite->setChecked(st.value("tftp/overwrite", false).toBool());
    auto *form = new QFormLayout;
    form->addRow(tr("Folder"), folderRow);
    form->addRow(tr("Address"), m_address);
    form->addRow(tr("Port"), m_port);
    form->addRow(QString(), m_uploads);
    form->addRow(QString(), m_overwrite);
    auto *box = new QVBoxLayout;
    box->addLayout(form);
    box->addWidget(hint(tr("TFTP has no passwords: anyone who can reach this port can read the "
                           "folder. Keep only what devices need in it, and stop the server when "
                           "you are done. It stops when sshLX closes.")));
    v->addWidget(card(tr("Settings"), box));

    m_usage = new QLabel;
    m_usage->setWordWrap(true);
    m_usage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_usage);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    v->addWidget(m_log, 1);

    m_toggle = primaryButton(tr("Start"));
    QPushButton *close = secondaryButton(tr("Close"));
    v->addWidget(footer({close, m_toggle}));

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("TFTP Folder"), m_folder->text());
        if (!d.isEmpty())
            m_folder->setText(d);
    });
    connect(m_uploads, &QCheckBox::toggled, m_overwrite, &QWidget::setEnabled);
    m_overwrite->setEnabled(m_uploads->isChecked());
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_toggle, &QPushButton::clicked, this, &TftpDialog::toggle);
    connect(server, &TftpServer::logLine, this, [this](const QString &line) {
        m_log->appendPlainText(line);
    });
    updateState();
}

void TftpDialog::toggle()
{
    if (m_server->running()) {
        m_server->stop();
        updateState();
        return;
    }
    QSettings st;
    st.setValue("tftp/folder", m_folder->text());
    st.setValue("tftp/address", m_address->currentData());
    st.setValue("tftp/port", m_port->value());
    st.setValue("tftp/uploads", m_uploads->isChecked());
    st.setValue("tftp/overwrite", m_overwrite->isChecked());

    TftpServer::Options o;
    o.root = m_folder->text();
    o.address = QHostAddress(m_address->currentData().toString());
    o.port = quint16(m_port->value());
    o.allowUploads = m_uploads->isChecked();
    o.allowOverwrite = m_uploads->isChecked() && m_overwrite->isChecked();
    QDir().mkpath(o.root);
    QString err;
    if (!m_server->start(o, &err)) {
        if (o.port < 1024)
            err += "\n\n" + tr("Linux only lets ordinary programs use ports from 1024 up. Either "
                               "pick a higher port and give it to the device, or allow port %1 "
                               "with:\n\nsudo sysctl -w net.ipv4.ip_unprivileged_port_start=%1\n\n"
                               "That lasts until a restart, and applies to every program.")
                                  .arg(o.port);
        DialogKit::error(this, tr("TFTP server not started"), err);
    }
    updateState();
}

void TftpDialog::updateState()
{
    const bool on = m_server->running();
    m_toggle->setText(on ? tr("Stop") : tr("Start"));
    for (QWidget *w : QList<QWidget *>{m_folder, m_address, m_port, m_uploads})
        w->setEnabled(!on);
    m_overwrite->setEnabled(!on && m_uploads->isChecked());
    if (!on) {
        m_usage->clear();
        return;
    }
    QString ip = m_address->currentData().toString();
    if (ip == QHostAddress(QHostAddress::AnyIPv4).toString()) {
        ip = tr("<this computer>");
        for (const QHostAddress &a : QNetworkInterface::allAddresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
                ip = a.toString();
                break;
            }
    }
    const QString port = m_server->port() == 69 ? QString() : QStringLiteral(":%1").arg(m_server->port());
    m_usage->setText(tr("On a Cisco device: copy tftp://%1%2/FILE flash:").arg(ip, port));
}
