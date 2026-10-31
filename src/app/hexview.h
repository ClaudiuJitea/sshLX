// HexViewDialog - what a session receives, byte by byte, from when it opens.
#pragma once

#include <QDialog>

#include "hexdump.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;

class HexViewDialog : public QDialog {
    Q_OBJECT
public:
    HexViewDialog(const QString &title, QWidget *parent);
    QString text() const;

public slots:
    void addData(const QByteArray &data);

private:
    HexDump m_dump;
    QPlainTextEdit *m_text;
    QLabel *m_tail, *m_count;
    QPushButton *m_pause;
};
