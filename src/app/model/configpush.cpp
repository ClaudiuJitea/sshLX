#include "configpush.h"

#include <QStringList>

namespace {

// A device asking something. Checked before the prompt: "[confirm]" would
// pass for a Huawei "[sysname]" prompt, which is why a bare [word] is no
// question here.
const QRegularExpression &questionRe()
{
    static const QRegularExpression re(
        R"((\[[^\]]*(confirm|/)[^\]]*\]\s*[?:]?|\[[^\]]*\]\s*[?:]|\([^)]*/[^)]*\)\s*[?:]?|\([^)]*\)\s*[?:])"
        R"(|\?|(password|passphrase|username)\s*:)\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &moreRe()
{
    static const QRegularExpression re(
        R"(^\s*(-+\s*\(?more\b.*|<-+\s*more\s*-+>|--more--.*|press any key.*|more:.*)\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Cisco's banner and macro commands read free text up to a delimiter,
// without prompting. "End with CNTL/Z" (conf t) has no quotes, so stays out.
const QRegularExpression &blockRe()
{
    static const QRegularExpression re(R"(End with (?:the character\s+)?'(.)')",
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

constexpr int kQuestionQuietMs = 700;  // silence before a tail counts as a question
constexpr int kBlockLineMs = 40;       // between the lines of a banner
constexpr int kRenameQuietMs = 3000;   // silence before a new device name counts

} // namespace

QString ConfigPush::defaultPromptPattern()
{
    // Router#  sw1(config-if)#  user@r1>  [user@host ~]$  <Huawei>  [Huawei-Vlan10]
    // RP/0/RSP0/CPU0:ios#  FortiGate (global) #
    return QStringLiteral(
        R"(^\s*(?:[\w.@:/~+\-\[\]()]+(?:\s+[\w.@:/~+\-\[\]()]+){0,3}\s?[#>$%]|<[\w.\-/]+>|\[[\w.\-/~@ ]+\])\s*$)");
}

QString ConfigPush::defaultErrorPattern()
{
    // "% Invalid input", but not "% Warning" or a "%SYS-5-CONFIG_I" syslog.
    return QStringLiteral(
        R"(^\s*%\s?(?!warning|note|info|[a-z0-9_]+-\d+-)\S)"
        R"(|^\s*\^\s*$)"
        R"(|^\s*(error|syntax error|unknown command|invalid)\b)"
        R"(|unrecognized command|invalid (input|command|parameter|value)|incomplete command)"
        R"(|ambiguous command|command not found|command rejected|command fail)");
}

QString ConfigPush::promptStem(const QString &prompt)
{
    QString t = prompt.trimmed();
    static const QRegularExpression ends(R"([#>$%\]]+\s*$)");
    t.remove(ends);
    t.remove(QRegularExpression(R"(^[<\[]+)"));
    QStringList words = t.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
    words.removeIf([](const QString &w) { return w.startsWith('('); });
    if (words.isEmpty())
        return {};
    QString word = words.first();
    for (const QString &w : std::as_const(words))
        if (w.contains('@'))
            word = w;
    return word.section(QRegularExpression("[:(]"), 0, 0);
}

bool ConfigPush::looksLikePrompt(const QString &line) const
{
    // "Reading database ... 45%" is progress, not a csh prompt.
    static const QRegularExpression percent(R"(\d\s*%\s*$)");
    return m_prompt.match(line).hasMatch() && !percent.match(line).hasMatch();
}

bool ConfigPush::isOurPrompt(const QString &line) const
{
    return looksLikePrompt(line) && (m_stem.isEmpty() || line.contains(m_stem));
}

// The line just sent gives the device a new name, and so a new prompt.
bool ConfigPush::renamesDevice() const
{
    static const QRegularExpression re(R"(^\s*(set\s+)?(hostname|sysname|host-name|system\s+host-name)\b)",
                                       QRegularExpression::CaseInsensitiveOption);
    return m_index < m_lines.size() && re.match(m_lines[m_index].text).hasMatch();
}

QString ConfigPush::patternError(const QString &pattern)
{
    QRegularExpression re(pattern);
    return re.isValid() ? QString() : re.errorString();
}

ConfigPush::ConfigPush(QObject *parent) : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(settleMs());
    connect(&m_settle, &QTimer::timeout, this, &ConfigPush::evaluate);

    m_quietCheck.setSingleShot(true);
    m_quietCheck.setInterval(kQuestionQuietMs);
    connect(&m_quietCheck, &QTimer::timeout, this, [this] {
        if (m_step != Step::WaitPrompt && m_step != Step::Sync && m_step != Step::WaitReady)
            return;
        const QString t = tail();
        if (questionRe().match(t).hasMatch()) {
            m_timeout.stop();
            pauseWith(Pause::Question, t.trimmed());
        }
    });

    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (questionRe().match(tail()).hasMatch())
            pauseWith(Pause::Question, tail().trimmed());
        else if (m_step == Step::Sync)
            pauseWith(Pause::NoPrompt, tail().trimmed());
        else if (m_step == Step::WaitPrompt || m_step == Step::WaitReady)
            pauseWith(Pause::Timeout, outputSinceSend().trimmed());
    });
}

void ConfigPush::start(const QStringList &lines, const Options &options)
{
    m_opts = options;
    m_prompt = QRegularExpression(options.promptPattern);
    m_error = QRegularExpression(options.errorPattern,
                                 QRegularExpression::CaseInsensitiveOption);
    m_lines.clear();
    for (const QString &raw : lines) {
        QString text = raw;
        while (text.endsWith('\r') || text.endsWith(' ') || text.endsWith('\t'))
            text.chop(1);
        const QString trimmed = text.trimmed();
        if (options.skipBlank && trimmed.isEmpty())
            continue;
        if (options.skipComments && trimmed.startsWith('!'))
            continue;
        Line l;
        l.text = l.original = text;
        m_lines.append(l);
    }
    m_index = 0;
    m_pause = Pause::None;
    m_pauseWanted = false;
    m_quiet = false;
    m_blockEnd = QChar();
    m_escape = 0;
    m_pendingCr = false;
    m_stem.clear();
    m_timeout.setInterval(options.timeoutMs);
    if (m_lines.isEmpty()) {
        finish(true);
        return;
    }
    // Press Enter first and see a prompt, so a wrong prompt pattern stops
    // here instead of on line one of a live device.
    m_step = Step::Sync;
    emit progress(0, total());
    QTimer::singleShot(0, this, [this] {
        if (m_step != Step::Sync)
            return;
        m_out.clear();
        m_lineStart = 0;
        m_timeout.setInterval(qMin(m_opts.timeoutMs, 5000));
        m_timeout.start();
        emit send(QString());
    });
}

// ---- output ------------------------------------------------------------------
void ConfigPush::feed(const char *data, size_t len)
{
    if (m_step == Step::Idle)
        return;
    acceptText(QString(m_decoder.decode(QByteArrayView(data, qsizetype(len)))));
    onOutput();
}

void ConfigPush::acceptText(const QString &text)
{
    for (QChar c : text) {
        ushort u = c.unicode();
        switch (m_escape) {
        case 1:
            m_escape = u == '[' ? 2 : (u == ']' || u == 'P') ? 3 : 0;
            continue;
        case 2:
            if (u >= 0x40 && u <= 0x7e)
                m_escape = 0;
            continue;
        case 3:
            if (u == 0x07)
                m_escape = 0;
            else if (u == 0x1b)
                m_escape = 4;
            continue;
        case 4:
            m_escape = u == '\\' ? 0 : 3;
            continue;
        }
        if (m_pendingCr) {
            m_pendingCr = false;
            if (u != '\n' && u != '\r')
                m_out.truncate(m_lineStart);   // a lone CR rewrites the line
        }
        switch (u) {
        case 0x1b: m_escape = 1; break;
        case '\r': m_pendingCr = true; break;
        case '\n':
            m_out += '\n';
            m_lineStart = m_out.size();
            break;
        case '\b':
            if (m_out.size() > m_lineStart)
                m_out.chop(1);
            break;
        default:
            if (u >= 0x20 || u == '\t')
                m_out += c;
        }
    }
}

QString ConfigPush::tail() const { return m_out.mid(m_lineStart); }

bool ConfigPush::sawEcho() const { return m_lineStart > 0; }

void ConfigPush::onOutput()
{
    if (m_step == Step::Paused) {
        // Answered in the terminal: the line goes on once it has really ended.
        if ((m_pause == Pause::Question || m_pause == Pause::Timeout) && isOurPrompt(tail())) {
            m_settle.setInterval(settleMs());
            m_settle.start();
        } else {
            m_settle.stop();
        }
        return;
    }
    if (m_step == Step::Between || m_step == Step::Idle)
        return;
    m_timeout.start();     // any output means it is alive: a slow "write mem" is fine
    m_settle.stop();
    m_quietCheck.stop();

    const QString t = tail();
    if (moreRe().match(t).hasMatch()) {
        m_out.truncate(m_lineStart);
        QTimer::singleShot(0, this, [this] {
            if (running())
                emit sendKeys(QStringLiteral(" "));
        });
        return;
    }
    if (m_step == Step::WaitPrompt) {
        auto block = blockRe().match(m_out);
        if (block.hasMatch() && m_blockEnd.isNull()) {
            m_blockEnd = block.captured(1).at(0);
            m_step = Step::TextBlock;
            m_timeout.stop();
            lineDone();
            return;
        }
    }
    if (m_step == Step::TextBlock)
        return;
    if (questionRe().match(t).hasMatch()) {
        m_quietCheck.start();
        return;
    }
    if (m_step == Step::Sync) {
        if (sawEcho() && looksLikePrompt(t)) {
            m_settle.setInterval(settleMs());
            m_settle.start();
        }
        return;
    }
    // Only this device's own prompt ends a line. Output that merely looks like
    // a prompt must not: the command is still running, and the next line
    // would go to it.
    if (sawEcho() && isOurPrompt(t)) {
        m_settle.setInterval(settleMs());
        m_settle.start();
    } else if (sawEcho() && looksLikePrompt(t) && renamesDevice()) {
        m_settle.setInterval(kRenameQuietMs);
        m_settle.start();
    }
}

// Whatever came back for the current line, without its echo and the prompt.
QString ConfigPush::outputSinceSend() const
{
    QStringList rows = m_out.left(m_lineStart).split('\n');
    if (!rows.isEmpty() && rows.last().isEmpty())
        rows.removeLast();
    if (!rows.isEmpty() && m_step != Step::Sync && m_index < m_lines.size()) {
        const QString sent = m_lines[m_index].text.trimmed();
        // A long line may come back shifted ("$ ...") and cut off.
        const QString row = rows.first();
        if (row.contains(sent.left(20)) || sent.endsWith(row.right(20).trimmed()))
            rows.removeFirst();
    }
    return rows.join('\n');
}

void ConfigPush::evaluate()
{
    if (m_step == Step::Paused) {
        if (m_pause != Pause::Question && m_pause != Pause::Timeout)
            return;
        m_pause = Pause::None;
        m_step = m_resumeStep;
        emit resumed();
    }
    if (looksLikePrompt(tail()))
        m_stem = promptStem(tail());     // also follows a change of name
    if (m_step == Step::WaitReady) {
        m_timeout.stop();
        m_step = Step::Between;
        advance();
        return;
    }
    if (m_step == Step::Sync) {
        m_timeout.stop();
        m_step = Step::Between;
        m_timeout.setInterval(m_opts.timeoutMs);
        advance();
        return;
    }
    if (m_step != Step::WaitPrompt)
        return;
    m_timeout.stop();
    const QString output = outputSinceSend();
    for (const QString &row : output.split('\n')) {
        if (!m_error.match(row).hasMatch())
            continue;
        m_lines[m_index].state = LineState::Failed;
        m_lines[m_index].error = output.trimmed();
        if (m_opts.stopOnError && !m_quiet) {
            pauseWith(Pause::DeviceError, output.trimmed());
            return;
        }
        m_step = Step::Between;
        m_index++;
        advance();
        return;
    }
    lineDone();
}

// ---- moving through the lines -------------------------------------------------
void ConfigPush::lineDone()
{
    m_lines[m_index].state = LineState::Ok;
    m_lines[m_index].error.clear();
    if (m_step != Step::TextBlock)
        m_step = Step::Between;
    m_index++;
    advance();
}

void ConfigPush::advance()
{
    emit progress(m_index, total());
    if (m_index >= total()) {
        finish(true);
        return;
    }
    if (m_pauseWanted && m_step != Step::TextBlock) {
        m_pauseWanted = false;
        pauseWith(Pause::ByUser, QString());
        return;
    }
    int delay = m_step == Step::TextBlock ? kBlockLineMs : m_opts.lineDelayMs;
    QTimer::singleShot(delay, this, [this, index = m_index] {
        if (running() && m_index == index && m_step != Step::Paused)
            sendCurrent();
    });
}

void ConfigPush::sendCurrent()
{
    m_out.clear();
    m_lineStart = 0;
    m_pendingCr = false;
    const QString text = m_lines[m_index].text;
    if (m_step == Step::TextBlock) {
        // Free text gets no prompt until the closing delimiter, and that
        // one is then an ordinary line waiting for the prompt back.
        if (text.contains(m_blockEnd)) {
            m_blockEnd = QChar();
            m_step = Step::WaitPrompt;
            m_timeout.start();
            emit send(text);
            return;
        }
        emit send(text);
        lineDone();
        return;
    }
    m_step = Step::WaitPrompt;
    m_timeout.start();
    emit send(text);
}

void ConfigPush::pauseWith(Pause reason, const QString &output)
{
    m_settle.stop();
    m_quietCheck.stop();
    m_timeout.stop();
    m_resumeStep = m_step;
    m_step = Step::Paused;
    m_pause = reason;
    emit paused(reason, output);
}

void ConfigPush::retry(const QString &text)
{
    if (m_pause == Pause::NoPrompt) {
        // Nothing has been sent yet: it may just need another Enter.
        m_pause = Pause::None;
        m_step = Step::Sync;
        m_out.clear();
        m_lineStart = 0;
        m_timeout.start();
        emit send(QString());
        return;
    }
    if (m_step != Step::Paused || m_index >= total())
        return;
    m_lines[m_index].text = text;
    m_lines[m_index].state = LineState::Pending;
    m_lines[m_index].error.clear();
    m_pause = Pause::None;
    m_step = Step::Between;
    sendCurrent();
}

void ConfigPush::skip()
{
    if (m_step != Step::Paused || m_index >= total())
        return;
    if (m_pause == Pause::NoPrompt) {
        // The user vouches that the device is ready.
        m_pause = Pause::None;
        m_step = Step::Between;
        m_timeout.setInterval(m_opts.timeoutMs);
        advance();
        return;
    }
    m_lines[m_index].state = LineState::Skipped;
    const bool ready = looksLikePrompt(tail());
    m_pause = Pause::None;
    m_index++;
    if (ready || m_index >= total()) {
        m_step = Step::Between;
        advance();
        return;
    }
    // It is still running: the next line waits for the prompt.
    emit progress(m_index, total());
    m_step = Step::WaitReady;
    m_timeout.start();
    onOutput();
}

void ConfigPush::keepWaiting()
{
    if (m_step != Step::Paused)
        return;
    m_pause = Pause::None;
    m_step = m_resumeStep;
    m_timeout.start();
    // The user has seen this prompt and says the line is done. A new prompt
    // (su, a jump to another host) then counts from here on.
    if (looksLikePrompt(tail()) && !isOurPrompt(tail()))
        m_stem = promptStem(tail());
    onOutput();    // it may have answered while the user was reading
}

bool ConfigPush::looksLikePasswordPrompt(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("(pass ?(word|phrase|code)|\\bpin\\b)[^\\n]*[:?]\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

void ConfigPush::answer(const QString &text)
{
    if (m_step != Step::Paused)
        return;
    m_pause = Pause::None;
    m_step = m_resumeStep;
    m_timeout.start();
    emit send(text);
}

void ConfigPush::ignoreErrors()
{
    if (m_step != Step::Paused || m_pause != Pause::DeviceError)
        return;
    m_quiet = true;
    m_pause = Pause::None;
    m_step = Step::Between;
    m_index++;
    advance();
}

int ConfigPush::failedCount() const
{
    int n = 0;
    for (const Line &l : m_lines)
        n += l.state == LineState::Failed || l.state == LineState::Skipped;
    return n;
}

void ConfigPush::retryFailed()
{
    if (running())
        return;
    QStringList again;
    for (const Line &l : m_lines)
        if (l.state == LineState::Failed || l.state == LineState::Skipped)
            again.append(l.text);
    if (!again.isEmpty())
        start(again, m_opts);
}

void ConfigPush::requestPause()
{
    if (running() && m_step != Step::Paused)
        m_pauseWanted = true;
}

void ConfigPush::resume()
{
    m_pauseWanted = false;
    if (m_step != Step::Paused || m_pause != Pause::ByUser)
        return;
    m_pause = Pause::None;
    m_step = Step::Between;
    advance();
}

void ConfigPush::stop()
{
    if (running())
        finish(false);
}

void ConfigPush::connectionLost()
{
    if (running())
        finish(false);
}

void ConfigPush::finish(bool complete)
{
    m_settle.stop();
    m_quietCheck.stop();
    m_timeout.stop();
    m_step = Step::Idle;
    m_pause = Pause::None;
    emit finished(complete);
}

QString ConfigPush::report() const
{
    int ok = 0, failed = 0, skipped = 0, pending = 0;
    QStringList details;
    for (int i = 0; i < m_lines.size(); i++) {
        const Line &l = m_lines[i];
        const QString where = QStringLiteral("Line %1: %2").arg(i + 1).arg(l.text);
        switch (l.state) {
        case LineState::Ok:
            ok++;
            if (l.text != l.original)
                details << where + QStringLiteral("  (amended from: %1)").arg(l.original);
            break;
        case LineState::Failed:
            failed++;
            details << where + QStringLiteral("  FAILED\n    ") +
                           QString(l.error).replace('\n', QStringLiteral("\n    "));
            break;
        case LineState::Skipped:
            skipped++;
            details << where + QStringLiteral("  SKIPPED");
            break;
        case LineState::Pending:
            pending++;
            break;
        }
    }
    QString head = QStringLiteral("%1 applied, %2 failed, %3 skipped, %4 not sent")
                       .arg(ok).arg(failed).arg(skipped).arg(pending);
    return details.isEmpty() ? head : head + '\n' + details.join('\n');
}
