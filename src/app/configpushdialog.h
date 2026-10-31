// ConfigPushDialog - review a configuration before it is sent line by line,
// and set how the device's replies are read. The settings are remembered.
#pragma once

#include <QDialog>

#include "configpush.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

class ConfigPushDialog : public QDialog {
    Q_OBJECT
public:
    ConfigPushDialog(const QString &text, const QString &sessionName, QWidget *parent);

    QStringList lines() const;
    void setText(const QString &text);
    ConfigPush::Options options() const;

private:
    void loadFile();
    void updateCount();
    void start();

    QPlainTextEdit *m_text;
    QLabel *m_count;
    class QPushButton *m_go;
    QSpinBox *m_timeout, *m_delay;
    QCheckBox *m_skipBlank, *m_skipComments, *m_stopOnError;
    QLineEdit *m_prompt, *m_error;
};
