#include "configpushdialog.h"

#include "dialogkit.h"

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace DialogKit;

ConfigPushDialog::ConfigPushDialog(const QString &text, const QString &sessionName,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Paste Configuration"));
    resize(760, 680);
    QSettings st;
    const ConfigPush::Options def;

    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("Paste Configuration"),
                        tr("Sent to %1 one line at a time, waiting for the prompt after each. "
                           "If the device reports an error, sshLX stops and asks.")
                            .arg(sessionName)));
    m_text = new QPlainTextEdit(text);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setPlaceholderText(tr("configure terminal\ninterface GigabitEthernet0/1\n"
                                  " description uplink\n no shutdown\nend"));
    v->addWidget(m_text, 1);

    m_timeout = new QSpinBox;
    m_timeout->setRange(1, 3600);
    m_timeout->setSuffix(tr(" s"));
    m_timeout->setValue(st.value("configPush/timeoutMs", def.timeoutMs).toInt() / 1000);
    m_delay = new QSpinBox;
    m_delay->setRange(0, 60000);
    m_delay->setSingleStep(50);
    m_delay->setSuffix(tr(" ms"));
    m_delay->setValue(st.value("configPush/lineDelayMs", def.lineDelayMs).toInt());
    m_skipBlank = new QCheckBox(tr("Leave out blank lines"));
    m_skipBlank->setChecked(st.value("configPush/skipBlank", def.skipBlank).toBool());
    m_skipComments = new QCheckBox(tr("Leave out comments (lines starting with !)"));
    m_skipComments->setChecked(st.value("configPush/skipComments", def.skipComments).toBool());
    m_stopOnError = new QCheckBox(tr("Stop and ask on every error"));
    m_stopOnError->setChecked(st.value("configPush/stopOnError", def.stopOnError).toBool());
    m_stopOnError->setToolTip(tr("Otherwise errors are only noted in the report."));
    m_prompt = new QLineEdit(st.value("configPush/prompt", def.promptPattern).toString());
    m_error = new QLineEdit(st.value("configPush/error", def.errorPattern).toString());

    auto *opts = new QFormLayout;
    opts->addRow(tr("Ask after no reply for"), m_timeout);
    opts->addRow(tr("Extra delay between lines"), m_delay);
    opts->addRow(QString(), m_skipBlank);
    opts->addRow(QString(), m_skipComments);
    opts->addRow(QString(), m_stopOnError);
    opts->addRow(tr("Prompt pattern"), m_prompt);
    opts->addRow(tr("Error pattern"), m_error);
    auto *box = new QVBoxLayout;
    box->addLayout(opts);
    auto *restore = secondaryButton(tr("Default Patterns"));
    connect(restore, &QPushButton::clicked, this, [this, def] {
        m_prompt->setText(def.promptPattern);
        m_error->setText(def.errorPattern);
    });
    box->addWidget(restore, 0, Qt::AlignRight);
    QFrame *options = card(tr("Options"), box);
    options->setVisible(false);
    v->addWidget(options);

    auto *more = secondaryButton(tr("Options"));
    more->setCheckable(true);
    connect(more, &QPushButton::toggled, options, &QWidget::setVisible);
    auto *load = secondaryButton(tr("Load File..."));
    connect(load, &QPushButton::clicked, this, &ConfigPushDialog::loadFile);
    m_count = new QLabel;
    auto *left = new QWidget;
    auto *lh = new QHBoxLayout(left);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->addWidget(load);
    lh->addWidget(more);
    lh->addWidget(m_count);

    QPushButton *cancel = secondaryButton(tr("Cancel"));
    QPushButton *go = primaryButton(tr("Start"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, &ConfigPushDialog::start);
    v->addWidget(footer({cancel, go}, left));

    connect(m_text, &QPlainTextEdit::textChanged, this, &ConfigPushDialog::updateCount);
    connect(m_skipBlank, &QCheckBox::toggled, this, &ConfigPushDialog::updateCount);
    connect(m_skipComments, &QCheckBox::toggled, this, &ConfigPushDialog::updateCount);
    updateCount();
}

QStringList ConfigPushDialog::lines() const
{
    return m_text->toPlainText().split('\n');
}

ConfigPush::Options ConfigPushDialog::options() const
{
    ConfigPush::Options o;
    o.promptPattern = m_prompt->text();
    o.errorPattern = m_error->text();
    o.timeoutMs = m_timeout->value() * 1000;
    o.lineDelayMs = m_delay->value();
    o.skipBlank = m_skipBlank->isChecked();
    o.skipComments = m_skipComments->isChecked();
    o.stopOnError = m_stopOnError->isChecked();
    return o;
}

void ConfigPushDialog::loadFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Configuration"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error(this, tr("Load Configuration"), f.errorString());
        return;
    }
    m_text->setPlainText(QString::fromUtf8(f.readAll()));
}

void ConfigPushDialog::updateCount()
{
    int n = 0;
    for (const QString &l : lines()) {
        const QString t = l.trimmed();
        if ((t.isEmpty() && m_skipBlank->isChecked()) ||
            (t.startsWith('!') && m_skipComments->isChecked()))
            continue;
        n++;
    }
    m_count->setText(tr("%n line(s) to send", nullptr, n));
}

void ConfigPushDialog::start()
{
    for (QLineEdit *e : {m_prompt, m_error}) {
        const QString problem = ConfigPush::patternError(e->text());
        if (!problem.isEmpty()) {
            error(this, tr("Paste Configuration"), tr("Bad pattern: %1").arg(problem));
            return;
        }
    }
    const ConfigPush::Options o = options();
    QSettings st;
    st.setValue("configPush/timeoutMs", o.timeoutMs);
    st.setValue("configPush/lineDelayMs", o.lineDelayMs);
    st.setValue("configPush/skipBlank", o.skipBlank);
    st.setValue("configPush/skipComments", o.skipComments);
    st.setValue("configPush/stopOnError", o.stopOnError);
    st.setValue("configPush/prompt", o.promptPattern);
    st.setValue("configPush/error", o.errorPattern);
    accept();
}
