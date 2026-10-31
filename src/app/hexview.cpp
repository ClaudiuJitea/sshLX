#include "hexview.h"

#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "dialogkit.h"

using namespace DialogKit;

HexViewDialog::HexViewDialog(const QString &title, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(title);
    resize(760, 460);
    auto *v = new QVBoxLayout(this);
    frame(v);
    m_text = new QPlainTextEdit;
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setMaximumBlockCount(50000);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_text->setFont(mono);
    v->addWidget(m_text, 1);
    m_tail = new QLabel;
    m_tail->setFont(mono);
    v->addWidget(m_tail);
    m_count = new QLabel;
    m_pause = secondaryButton(tr("Pause"));
    m_pause->setCheckable(true);
    auto *clear = secondaryButton(tr("Clear"));
    auto *save = secondaryButton(tr("Save..."));
    auto *close = primaryButton(tr("Close"));
    auto *left = new QWidget;
    auto *h = new QHBoxLayout(left);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(m_pause);
    h->addWidget(clear);
    h->addWidget(save);
    h->addWidget(m_count);
    v->addWidget(footer({close}, left));
    connect(m_pause, &QPushButton::toggled, this, [this](bool on) {
        m_pause->setText(on ? tr("Resume") : tr("Pause"));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_dump.clear();
        m_text->clear();
        m_tail->clear();
        m_count->clear();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save Received Data"), "received.txt");
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(text().toUtf8()) < 0)
            error(this, tr("Save Received Data"), f.errorString());
    });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
}

QString HexViewDialog::text() const
{
    return m_text->toPlainText() + '\n' + m_dump.tail();
}

void HexViewDialog::addData(const QByteArray &data)
{
    if (m_pause->isChecked())
        return;
    const QString lines = m_dump.append(data);
    if (!lines.isEmpty())
        m_text->appendPlainText(lines.chopped(1));
    m_tail->setText(m_dump.tail());
    m_count->setText(tr("%n byte(s)", nullptr, int(m_dump.total())));
}
