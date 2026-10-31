#include "logondialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "logonrunner.h"

using namespace DialogKit;

enum { ColWait, ColRegex, ColSend, ColReply };

LogonDialog::LogonDialog(const QList<LogonStep> &steps, const QList<Credential> &credentials,
                         bool folder, QWidget *parent)
    : QDialog(parent), m_credentials(credentials)
{
    setWindowTitle(tr("Logon Actions"));
    resize(760, 430);
    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("Logon Actions"), folder
        ? tr("Run after connecting to anything in this folder that has none of its own.")
        : tr("Run after connecting. Without any, the nearest folder's are used.")));
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({tr("Wait for"), tr("Regex"), tr("Then send"), tr("Reply with")});
    m_table->horizontalHeader()->setSectionResizeMode(ColWait, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColSend, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColReply, QHeaderView::Fixed);
    m_table->setColumnWidth(ColReply, 200);
    m_table->setColumnWidth(ColRegex, 60);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    v->addWidget(m_table, 1);
    v->addWidget(hint(tr("An empty \"Wait for\" goes straight on, and an empty \"Then send\" sends "
                         "Enter, which wakes a serial console. Each step looks only at output "
                         "after the last match. Passwords come from the keyring, and go only to "
                         "a line that looks like a password prompt.")));

    m_script = new QLineEdit;
    m_script->setObjectName("logonScript");
    m_script->setPlaceholderText(tr("Optional: a Python script to run once these steps are done"));
    auto *browse = secondaryButton(tr("Browse..."));
    auto *scriptRow = new QHBoxLayout;
    scriptRow->addWidget(m_script, 1);
    scriptRow->addWidget(browse);
    v->addLayout(scriptRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Python Script"), m_script->text(),
                                                          tr("Python scripts (*.py);;All files (*)"));
        if (!path.isEmpty())
            m_script->setText(path);
    });

    m_idleText = new QLineEdit;
    m_idleText->setPlaceholderText(tr("Anti-idle: text to send, such as \\r for Enter"));
    m_idleSeconds = new QSpinBox;
    m_idleSeconds->setRange(0, 86400);
    m_idleSeconds->setSuffix(tr(" s idle"));
    m_idleSeconds->setSpecialValueText(tr("Off"));
    auto *idleRow = new QHBoxLayout;
    idleRow->addWidget(m_idleText, 1);
    idleRow->addWidget(m_idleSeconds);
    v->addLayout(idleRow);

    auto *buttons = new QWidget;
    auto *bh = new QHBoxLayout(buttons);
    bh->setContentsMargins(0, 0, 0, 0);
    auto *add = secondaryButton(tr("Add"));
    auto *remove = secondaryButton(tr("Remove"));
    auto *up = secondaryButton(tr("Move Up"));
    auto *down = secondaryButton(tr("Move Down"));
    for (QPushButton *b : {add, remove, up, down})
        bh->addWidget(b);
    QPushButton *cancel = secondaryButton(tr("Cancel"));
    QPushButton *save = primaryButton(tr("Save"));
    v->addWidget(footer({cancel, save}, buttons));

    connect(add, &QPushButton::clicked, this, [this] { addStep({}); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (m_table->currentRow() >= 0)
            m_table->removeRow(m_table->currentRow());
    });
    connect(up, &QPushButton::clicked, this, [this] { moveRow(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveRow(+1); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] {
        for (const LogonStep &s : this->steps()) {
            const QString bad = s.regex ? LogonRunner::patternError(s.wait) : QString();
            if (!bad.isEmpty()) {
                error(this, tr("Logon Actions"), tr("\"%1\": %2").arg(s.wait, bad));
                return;
            }
        }
        accept();
    });
    for (const LogonStep &s : steps)
        addStep(s);
}

void LogonDialog::addStep(const LogonStep &step)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *wait = new QLineEdit(step.wait);
    wait->setPlaceholderText(tr("(nothing)"));
    auto *regex = new QCheckBox;
    regex->setChecked(step.regex);
    auto *send = new QLineEdit(step.send);
    auto *reply = new QComboBox;
    reply->addItem(tr("Text"), "text");
    reply->addItem(tr("Connection's password"), "");
    for (const Credential &c : m_credentials)
        reply->addItem(c.name, c.id);
    if (step.password) {
        const int i = reply->findData(step.credential);
        reply->setCurrentIndex(i >= 0 ? i : 1);
    }
    m_table->setCellWidget(row, ColWait, wait);
    m_table->setCellWidget(row, ColRegex, regex);
    m_table->setCellWidget(row, ColSend, send);
    m_table->setCellWidget(row, ColReply, reply);
    connect(reply, &QComboBox::currentIndexChanged, send, [send, reply] {
        send->setEnabled(reply->currentIndex() == 0);
    });
    send->setEnabled(reply->currentIndex() == 0);
    m_table->setCurrentCell(row, ColWait);
}

QString LogonDialog::antiIdleText() const { return m_idleText->text(); }

int LogonDialog::antiIdleSeconds() const { return m_idleSeconds->value(); }

void LogonDialog::setAntiIdle(const QString &text, int seconds)
{
    m_idleText->setText(text);
    m_idleSeconds->setValue(seconds);
}

QString LogonDialog::script() const
{
    return m_script->text().trimmed();
}

void LogonDialog::setScript(const QString &path)
{
    m_script->setText(path);
}

QList<LogonStep> LogonDialog::steps() const
{
    QList<LogonStep> r;
    for (int row = 0; row < m_table->rowCount(); row++) {
        LogonStep s;
        s.wait = qobject_cast<QLineEdit *>(m_table->cellWidget(row, ColWait))->text();
        s.regex = qobject_cast<QCheckBox *>(m_table->cellWidget(row, ColRegex))->isChecked();
        auto *reply = qobject_cast<QComboBox *>(m_table->cellWidget(row, ColReply));
        s.password = reply->currentIndex() != 0;
        if (s.password)
            s.credential = reply->currentData().toString();
        else
            s.send = qobject_cast<QLineEdit *>(m_table->cellWidget(row, ColSend))->text();
        r.append(s);
    }
    return r;
}

void LogonDialog::moveRow(int delta)
{
    const int row = m_table->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || to >= m_table->rowCount())
        return;
    QList<LogonStep> all = steps();
    all.move(row, to);
    m_table->setRowCount(0);
    for (const LogonStep &s : all)
        addStep(s);
    m_table->setCurrentCell(to, ColWait);
}
