// TftpDialog - starts and stops the TFTP server and shows what it does. The
// server belongs to the main window, so it keeps running with this closed.
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class TftpServer;

class TftpDialog : public QDialog {
    Q_OBJECT
public:
    TftpDialog(TftpServer *server, QWidget *parent);

private:
    void toggle();
    void updateState();

    TftpServer *m_server;
    QLineEdit *m_folder;
    QComboBox *m_address;
    QSpinBox *m_port;
    QCheckBox *m_uploads, *m_overwrite;
    QPushButton *m_toggle;
    QLabel *m_usage;
    QPlainTextEdit *m_log;
};
