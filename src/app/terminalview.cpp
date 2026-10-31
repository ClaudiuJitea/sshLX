#include "terminalview.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QIcon>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QStyleHints>
#include <QWheelEvent>

#include <cmath>

#include "fontspec.h"
#include "highlightrules.h"
#include "keymap.h"
#include "shortcuts.h"

TerminalView::TerminalView(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::IBeamCursor);
    setFonts(TerminalFonts{"client:Monospace 12", "", "", "", false, 1, 1});
}

TerminalView::~TerminalView()
{
    m_painter.reset();
}

void TerminalView::setFonts(const TerminalFonts &f)
{
    m_fontNames = f;
    FontSpec::Parsed n = FontSpec::fromPutty(f.normal);
    m_fallbackFont = n.serverFont;
    m_font = n.font;
    m_font.setStyleStrategy(QFont::PreferDefault);
    // Bold: a configured bold font, else the normal font emboldened.
    FontSpec::Parsed b = FontSpec::fromPutty(f.bold);
    m_haveBold = !b.empty && !b.serverFont && !f.shadowBold;
    m_boldFont = m_haveBold ? b.font : m_font;
    if (!m_haveBold && !f.shadowBold)
        m_boldFont.setWeight(QFont::Bold);
    FontSpec::Parsed w = FontSpec::fromPutty(f.wide);
    m_haveWide = !w.empty && !w.serverFont;
    m_wideFont = m_haveWide ? w.font : m_font;
    FontSpec::Parsed wb = FontSpec::fromPutty(f.wideBold);
    m_haveWideBold = !wb.empty && !wb.serverFont && !f.shadowBold;
    m_wideBoldFont = m_haveWideBold ? wb.font : m_boldFont;
    recomputeMetrics();
    recreateImage();
    updateGeometry();
    updateGrid();
}

void TerminalView::recomputeMetrics()
{
    QFontMetricsF fm(m_font, this);
    qreal adv = fm.horizontalAdvance(QLatin1Char('M'));
    m_cw = std::max(1, int(std::ceil(adv - 0.01)));
    m_ch = std::max(1, int(std::ceil(fm.ascent() + fm.descent() - 0.01)));
    m_ascent = int(std::round(fm.ascent()));
    m_strikeY = m_ascent - int(std::round(fm.strikeOutPos()));
    m_exactAdvance = std::abs(adv - m_cw) < 0.01 &&
        std::abs(fm.horizontalAdvance(QLatin1Char('i')) - adv) < 0.01;
}

QSize TerminalView::sizeForGrid(int cols, int rows) const
{
    int b = m_fontNames.border;
    return QSize(cols * m_cw + 2 * b, rows * m_ch + 2 * b);
}

QSize TerminalView::sizeHint() const
{
    return sizeForGrid(m_hintCols, m_hintRows);
}

QSize TerminalView::minimumSizeHint() const
{
    return sizeForGrid(10, 3);
}

void TerminalView::setDefaultBackground(QColor c)
{
    m_defaultBg = c;
    update();
}

void TerminalView::setRawMousePointer(bool raw)
{
    m_rawPointer = raw;
    if (!m_busy)
        setCursor(raw ? Qt::ArrowCursor : Qt::IBeamCursor);
}

void TerminalView::setBusy(bool busy)
{
    m_busy = busy;
    setCursor(busy ? Qt::BusyCursor :
              m_rawPointer ? Qt::ArrowCursor : Qt::IBeamCursor);
}

void TerminalView::setCursorCell(int col, int row)
{
    m_cursorCol = col;
    m_cursorRow = row;
    if (hasFocus())
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
}

void TerminalView::recreateImage()
{
    const qreal dpr = devicePixelRatioF();
    QSize phys(std::max(1, int(std::ceil(width() * dpr))),
               std::max(1, int(std::ceil(height() * dpr))));
    m_painter.reset();
    m_image = QImage(phys, QImage::Format_RGB32);
    m_image.setDevicePixelRatio(dpr);
    m_image.fill(m_defaultBg);
}

void TerminalView::updateGrid()
{
    int b = m_fontNames.border;
    int cols = std::max(1, (width() - 2 * b) / m_cw);
    int rows = std::max(1, (height() - 2 * b) / m_ch);
    bool changed = (cols != m_cols || rows != m_rows);
    m_cols = cols;
    m_rows = rows;
    if (changed) {
        m_cells.assign(qsizetype(cols) * rows, Cell());
        m_rowRules.assign(rows, QVector<int>());
        m_rowStale.assign(rows, true);
    }
    if (m_session) {
        if (changed)
            sshlx_session_resize(m_session, cols, rows);
        sshlx_session_invalidate(m_session);
    }
    if (changed)
        emit gridResized(cols, rows);
}

void TerminalView::resizeEvent(QResizeEvent *)
{
    recreateImage();
    updateGrid();
}

void TerminalView::paintEvent(QPaintEvent *e)
{
    if (m_image.isNull() ||
        m_image.devicePixelRatio() != devicePixelRatioF()) {
        recreateImage();
        if (m_session)
            sshlx_session_invalidate(m_session);
    }
    QPainter p(this);
    p.setClipRegion(e->region());
    p.drawImage(0, 0, m_image);
    if (overlayActive())
        paintHighlights(p, e->rect());
}

void TerminalView::setHighlights(const HighlightSet *set)
{
    m_highlights = set && !set->isEmpty() ? set : nullptr;
    m_rowStale.fill(true);
    update();
}

bool TerminalView::overlayActive() const
{
    return m_highlights || (m_find && !m_find->isEmpty());
}

void TerminalView::setFind(const QString &text, bool matchCase)
{
    QList<HighlightRule> rules;
    if (!text.isEmpty()) {
        HighlightRule r;
        r.pattern = text;
        r.matchCase = matchCase;
        r.wholeWord = false;
        rules.append(r);
    }
    if (!m_find)
        m_find = std::make_unique<HighlightSet>();
    m_find->setRules(rules);
    m_rowStale.fill(true);
    update();
}

void TerminalView::setFindCurrent(int row, int start, int len)
{
    m_findRow = row;
    m_findStart = start;
    m_findLen = len;
    m_rowStale.fill(true);
    update();
}

QString TerminalView::rowText(int row) const
{
    QString line;
    if (row < 0 || row >= m_rows || m_cells.size() != qsizetype(m_cols) * m_rows)
        return line;
    for (int c = 0; c < m_cols; c++) {
        char32_t ch = m_cells[row * m_cols + c].ch;
        if (ch)
            line += QString::fromUcs4(&ch, 1);
    }
    while (line.endsWith(' '))
        line.chop(1);
    return line;
}

static constexpr int kFindMatch = -2, kFindCurrent = -3;

void TerminalView::matchRow(int row)
{
    // Columns of each UTF-16 unit: wide and astral characters do not map 1:1.
    QString line;
    QVector<int> colOf;
    for (int c = 0; c < m_cols; c++) {
        char32_t ch = m_cells[row * m_cols + c].ch;
        if (!ch)
            continue;
        const QString s = QString::fromUcs4(&ch, 1);
        line += s;
        colOf.append(QVector<int>(s.size(), c));
    }
    QVector<int> &rules = m_rowRules[row];
    rules.assign(m_cols, -1);
    if (m_highlights) {
        const QVector<int> owner = m_highlights->match(line);
        for (int i = 0; i < owner.size(); i++)
            rules[colOf[i]] = owner[i];
    }
    if (m_find && !m_find->isEmpty()) {
        const QVector<int> owner = m_find->match(line);
        for (int i = 0; i < owner.size(); i++)
            if (owner[i] >= 0)
                rules[colOf[i]] = kFindMatch;
    }
    if (row == m_findRow)
        for (int i = m_findStart; i < m_findStart + m_findLen && i < colOf.size(); i++)
            rules[colOf[i]] = kFindCurrent;
    for (int c = 1; c < m_cols; c++)
        if (!m_cells[row * m_cols + c].ch)
            rules[c] = rules[c - 1];
    m_rowStale[row] = false;
}

// Keywords are drawn over the blitted image, so PuTTY's picture of the
// screen stays as it is. Cells on a background of their own are left out:
// that is the selection, the cursor, and whatever a program coloured.
void TerminalView::paintHighlights(QPainter &p, const QRect &area)
{
    if (m_cells.size() != qsizetype(m_cols) * m_rows)
        return;
    const int b = m_fontNames.border;
    const QRgb plainBg = m_defaultBg.rgb();
    static const QList<HighlightRule> noRules;
    const QList<HighlightRule> &rules = m_highlights ? m_highlights->rules() : noRules;
    const int first = std::max(0, (area.top() - b) / m_ch);
    const int last = std::min(m_rows - 1, (area.bottom() - b) / m_ch);
    for (int row = first; row <= last; row++) {
        if (m_rowStale[row])
            matchRow(row);
        const QVector<int> &owner = m_rowRules[row];
        for (int c = 0; c < m_cols; c++) {
            const int index = owner[c];
            const Cell &cell = m_cells[row * m_cols + c];
            const bool found = index == kFindMatch || index == kFindCurrent;
            if ((index < 0 && !found) || !cell.ch)
                continue;
            if (!found && (!cell.plain || cell.bg != plainBg))
                continue;
            const bool wide = c + 1 < m_cols && !m_cells[row * m_cols + c + 1].ch;
            const int cells = wide ? 2 : 1;
            const int x = b + c * m_cw, y = b + row * m_ch;
            QRect r(x, y, cells * m_cw, m_ch);
            QColor fg(cell.fg), bg(cell.bg);
            bool bold = cell.bold;
            if (found) {
                fg = Qt::black;
                bg = QColor(index == kFindCurrent ? "#ff922b" : "#ffe066");
            } else {
                const HighlightRule &rule = rules[index];
                if (!rule.fg.isEmpty())
                    fg = QColor(rule.fg);
                if (!rule.bg.isEmpty())
                    bg = QColor(rule.bg);
                bold = bold || rule.bold;
            }
            p.fillRect(r, bg);
            p.save();
            p.setClipRect(r);
            p.setFont(pickFont(bold, wide));
            p.setPen(fg);
            const uint32_t ch = cell.ch;
            drawGlyphs(p, x, y + m_ascent, &ch, 1, false, cells, m_exactAdvance && !bold);
            p.restore();
        }
    }
}

// ---- engine drawing --------------------------------------------------------
bool TerminalView::beginDraw()
{
    if (m_capturing)
        return true;
    if (m_image.isNull())
        return false;
    if (!m_painter) {
        m_painter = std::make_unique<QPainter>(&m_image);
    }
    m_dirty = QRect();
    return true;
}

void TerminalView::endDraw()
{
    if (m_painter)
        m_painter.reset();
    if (!m_dirty.isNull()) {
        // One new character can complete a keyword begun earlier on the row.
        if (overlayActive()) {
            m_dirty.setLeft(0);
            m_dirty.setRight(width());
        }
        update(m_dirty);
    }
    m_dirty = QRect();
}

const QFont &TerminalView::pickFont(bool bold, bool wide) const
{
    if (wide)
        return bold ? m_wideBoldFont : m_wideFont;
    return bold ? m_boldFont : m_font;
}

static inline QColor qc(sshlx_rgb c) { return QColor(c.r, c.g, c.b); }

void TerminalView::drawGlyphs(QPainter &p, qreal x, qreal baseline,
                              const uint32_t *text, int len, bool combining,
                              int cellsPerChar, bool exactAdvance)
{
    if (combining) {
        p.drawText(QPointF(x, baseline),
                   QString::fromUcs4(reinterpret_cast<const char32_t *>(text), len));
        return;
    }
    bool ascii = exactAdvance && cellsPerChar == 1;
    for (int i = 0; ascii && i < len; i++)
        if (text[i] < 0x20 || text[i] >= 0x7f)
            ascii = false;
    if (ascii) {
        p.drawText(QPointF(x, baseline),
                   QString::fromUcs4(reinterpret_cast<const char32_t *>(text), len));
        return;
    }
    for (int i = 0; i < len; i++) {
        if (text[i] == ' ')
            continue;
        char32_t c = text[i];
        QString s = QString::fromUcs4(&c, 1);
        qreal cx = x + i * cellsPerChar * m_cw;
        if (cellsPerChar > 1) {
            // centre wide glyphs in their two cells
            qreal adv = p.fontMetrics().horizontalAdvance(s);
            cx += std::max<qreal>(0, (cellsPerChar * m_cw - adv) / 2);
        }
        p.drawText(QPointF(cx, baseline), s);
    }
}

void TerminalView::drawText(int col, int row, int ncells, const uint32_t *text,
                            int len, const sshlx_text_style *st)
{
    if (row >= 0 && row < m_rows && m_cells.size() == qsizetype(m_cols) * m_rows) {
        const bool combining = st->flags & SSHLX_A_COMBINING;
        const int step = (st->flags & SSHLX_A_WIDE) ? 2 : 1;
        Cell cell;
        cell.fg = qRgb(st->fg.r, st->fg.g, st->fg.b);
        cell.bg = qRgb(st->bg.r, st->bg.g, st->bg.b);
        cell.bold = st->flags & SSHLX_A_BOLD_FONT;
        cell.plain = !combining && st->line_attr == SSHLX_LINE_NORMAL &&
            !(st->flags & (SSHLX_A_UNDER | SSHLX_A_STRIKE));
        const int chars = combining ? 1 : len;
        for (int i = 0; i < chars && col + i * step < m_cols; i++) {
            cell.ch = text[i];
            m_cells[row * m_cols + col + i * step] = cell;
            if (step == 2 && col + i * 2 + 1 < m_cols) {
                cell.ch = 0;
                m_cells[row * m_cols + col + i * 2 + 1] = cell;
            }
        }
        if (!m_rowStale.isEmpty())
            m_rowStale[row] = true;
    }
    if (!m_painter)
        return;
    QPainter &p = *m_painter;
    const int b = m_fontNames.border;
    const int x = b + col * m_cw, y = b + row * m_ch;
    const int w = ncells * m_cw;
    QRect cellRect(x, y, w, m_ch);
    p.fillRect(cellRect, qc(st->bg));

    const bool wide = st->flags & SSHLX_A_WIDE;
    const bool combining = st->flags & SSHLX_A_COMBINING;
    const bool boldFont = (st->flags & SSHLX_A_BOLD_FONT) &&
        !(st->flags & SSHLX_A_SHADOW_BOLD);
    const bool shadow = (st->flags & SSHLX_A_BOLD_FONT) &&
        (st->flags & SSHLX_A_SHADOW_BOLD);
    const int cellsPerChar = wide ? 2 : 1;
    const QFont &font = pickFont(boldFont, wide);
    const bool exact = m_exactAdvance && !boldFont;

    p.save();
    p.setClipRect(cellRect);
    p.setFont(font);
    p.setPen(qc(st->fg));
    if (st->line_attr != SSHLX_LINE_NORMAL) {
        // Double-width (and double-height) lines: col/ncells are already in
        // doubled units; draw at normal size and stretch.
        p.translate(x, y);
        bool tall = st->line_attr == SSHLX_LINE_TOP ||
            st->line_attr == SSHLX_LINE_BOTTOM;
        p.scale(2, tall ? 2 : 1);
        if (st->line_attr == SSHLX_LINE_BOTTOM)
            p.translate(0, -m_ch / 2.0);
        int n = combining ? 1 : len;
        drawGlyphs(p, 0, m_ascent, text, len, combining, cellsPerChar, exact);
        if (shadow)
            drawGlyphs(p, st->shadow_bold_offset / 2.0, m_ascent, text, len,
                       combining, cellsPerChar, exact);
        int uy = std::min(m_ascent + 1, m_ch - 1);
        if (st->flags & SSHLX_A_UNDER)
            p.drawLine(QLineF(0, uy + 0.5, n * cellsPerChar * m_cw, uy + 0.5));
        if (st->flags & SSHLX_A_STRIKE)
            p.drawLine(QLineF(0, m_strikeY + 0.5, n * cellsPerChar * m_cw,
                              m_strikeY + 0.5));
    } else {
        drawGlyphs(p, x, y + m_ascent, text, len, combining, cellsPerChar, exact);
        if (shadow)
            drawGlyphs(p, x + st->shadow_bold_offset, y + m_ascent, text, len,
                       combining, cellsPerChar, exact);
        int n = combining ? 1 : len;
        // As window.c: underline at ascent+1, clipped to the cell.
        int uy = std::min(m_ascent + 1, m_ch - 1);
        if (st->flags & SSHLX_A_UNDER)
            p.fillRect(QRect(x, y + uy, n * cellsPerChar * m_cw, 1), qc(st->fg));
        if (st->flags & SSHLX_A_STRIKE)
            p.fillRect(QRect(x, y + m_strikeY, n * cellsPerChar * m_cw, 1),
                       qc(st->fg));
    }
    p.restore();
    m_dirty |= cellRect;
}

void TerminalView::drawCursorShape(int col, int row, int ncells, int shape,
                                   bool passive, bool rightEdge,
                                   sshlx_rgb colour, int lineAttr)
{
    if (!m_painter)
        return;
    QPainter &p = *m_painter;
    const int b = m_fontNames.border;
    const int x = b + col * m_cw, y = b + row * m_ch;
    const int w = ncells * m_cw;
    const QColor c = qc(colour);
    (void)lineAttr;
    p.save();
    p.setPen(Qt::NoPen);
    if (shape == SSHLX_CURSOR_BOX_OUTLINE) {
        p.fillRect(QRect(x, y, w, 1), c);
        p.fillRect(QRect(x, y + m_ch - 1, w, 1), c);
        p.fillRect(QRect(x, y, 1, m_ch), c);
        p.fillRect(QRect(x + w - 1, y, 1, m_ch), c);
    } else if (shape == SSHLX_CURSOR_UNDERLINE) {
        int uy = std::min(m_ascent + 1, m_ch - 1);
        for (int i = 0; i < w; i++)
            if (!passive || i % 2 == 0)
                p.fillRect(QRect(x + i, y + uy, 1, 1), c);
    } else {
        int vx = rightEdge ? x + w - 1 : x;
        for (int i = 0; i < m_ch; i++)
            if (!passive || i % 2 == 0)
                p.fillRect(QRect(vx, y + i, 1, 1), c);
    }
    p.restore();
    m_dirty |= QRect(x, y, w, m_ch);
}

void TerminalView::drawTrustSigil(int col, int row)
{
    if (!m_painter)
        return;
    const int b = m_fontNames.border;
    QRect r(b + col * m_cw, b + row * m_ch, 2 * m_cw, m_ch);
    QIcon icon = QApplication::windowIcon();
    int side = std::min(r.width(), r.height());
    QRect sq(r.x() + (r.width() - side) / 2, r.y() + (r.height() - side) / 2,
             side, side);
    icon.paint(m_painter.get(), sq);
    m_dirty |= r;
}

// ---- keyboard ----------------------------------------------------------------
bool TerminalView::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(e);
        // The terminal gets every key except the configured application
        // shortcuts, unless "send next shortcut to terminal" is armed.
        if (Shortcuts::instance().terminalShouldTake(ke)) {
            e->accept();
            return true;
        }
        return QWidget::event(e);
    }
    if (e->type() == QEvent::KeyPress) {
        // Make sure Tab/Backtab reach keyPressEvent instead of moving focus.
        keyPressEvent(static_cast<QKeyEvent *>(e));
        return true;
    }
    return QWidget::event(e);
}

void TerminalView::keyPressEvent(QKeyEvent *e)
{
    if (!m_session)
        return;
    if (e->key() == Qt::Key_Menu && e->modifiers() == Qt::NoModifier) {
        const int b = m_fontNames.border;
        emit rightClicked(mapToGlobal(QPoint(b + m_cursorCol * m_cw, b + (m_cursorRow + 1) * m_ch)),
                          QPoint(m_cursorCol, m_cursorRow), true);
        e->accept();
        return;
    }
    if (m_inputBlocked) {
        e->accept();
        return;
    }
    if (sshlx_session_state(m_session) == SSHLX_STATE_DISCONNECTED) {
        emit keyPressedWhileDisconnected();
    }
    sshlx_key_event ev{};
    if (!KeyMap::translate(e, false, &ev, &m_keyText))
        return;
    switch (sshlx_session_key(m_session, &ev)) {
    case SSHLX_KEY_FONT_BIGGER: emit fontSizeStep(+1); break;
    case SSHLX_KEY_FONT_SMALLER: emit fontSizeStep(-1); break;
    default: break;
    }
    e->accept();
}

void TerminalView::keyReleaseEvent(QKeyEvent *e)
{
    if (!m_session || e->isAutoRepeat())
        return;
    // Releases matter only for PuTTY's Alt+numeric-keypad codes.
    if (e->key() != Qt::Key_Alt && e->key() != Qt::Key_Meta)
        return;
    sshlx_key_event ev{};
    if (KeyMap::translate(e, true, &ev, &m_keyText))
        sshlx_session_key(m_session, &ev);
}

void TerminalView::inputMethodEvent(QInputMethodEvent *e)
{
    if (!m_session)
        return;
    if (!e->commitString().isEmpty())
        sshlx_session_text(m_session, e->commitString().toUtf8().constData());
    if (!e->preeditString().isEmpty())
        sshlx_session_preedit(m_session, e->preeditString().toUtf8().constData());
    else
        sshlx_session_preedit(m_session, nullptr);
    e->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery q) const
{
    const int b = m_fontNames.border;
    switch (q) {
    case Qt::ImCursorRectangle:
        return QRect(b + m_cursorCol * m_cw, b + m_cursorRow * m_ch, m_cw, m_ch);
    case Qt::ImFont:
        return m_font;
    case Qt::ImEnabled:
        return true;
    default:
        return QWidget::inputMethodQuery(q);
    }
}

// ---- mouse -------------------------------------------------------------------
QPoint TerminalView::cellAt(const QPointF &pos) const
{
    const int b = m_fontNames.border;
    return QPoint(int(std::floor((pos.x() - b) / m_cw)),
                  int(std::floor((pos.y() - b) / m_ch)));
}

static sshlx_mbutton mapButton(Qt::MouseButton b)
{
    switch (b) {
    case Qt::LeftButton: return SSHLX_MB_LEFT;
    case Qt::MiddleButton: return SSHLX_MB_MIDDLE;
    case Qt::RightButton: return SSHLX_MB_RIGHT;
    default: return SSHLX_MB_NOTHING;
    }
}

void TerminalView::mouseButton(QMouseEvent *e, sshlx_maction action)
{
    if (!m_session)
        return;
    QPoint cell = cellAt(e->position());
    sshlx_mbutton b = mapButton(e->button());
    if (b == SSHLX_MB_NOTHING)
        return;
    bool consumed = sshlx_session_mouse(m_session, b, action, cell.x(), cell.y(),
                                        KeyMap::modifiers(e->modifiers()));
    if (!consumed && b == SSHLX_MB_RIGHT && action == SSHLX_MA_CLICK)
        emit rightClicked(e->globalPosition().toPoint(), cell,
                          e->modifiers() & Qt::ControlModifier);
}

QString TerminalView::linkAt(const QPoint &cell) const
{
    if (cell.y() < 0 || cell.y() >= m_rows || cell.x() < 0 || cell.x() >= m_cols ||
        m_cells.size() != m_cols * m_rows)
        return {};
    QString line;
    int textCol = -1;
    for (int c = 0; c < m_cols; c++) {
        if (c == cell.x())
            textCol = line.size();
        char32_t ch = m_cells[cell.y() * m_cols + c].ch;
        if (ch)
            line += QString::fromUcs4(&ch, 1);
    }
    static const QRegularExpression re(R"(https?://[^\s<>"'`]+)");
    for (auto it = re.globalMatch(line); it.hasNext();) {
        QRegularExpressionMatch m = it.next();
        QString url = m.captured();
        while (!url.isEmpty() && QStringLiteral(".,;:!?)]}").contains(url.back()))
            url.chop(1);
        if (textCol >= m.capturedStart() && textCol < m.capturedStart() + url.size())
            return url;
    }
    return {};
}

void TerminalView::mousePressEvent(QMouseEvent *e)
{
    setFocus(Qt::MouseFocusReason);
    QPoint cell = cellAt(e->position());
    if (e->button() == Qt::LeftButton && e->modifiers() == Qt::ControlModifier &&
        !m_rawPointer) {
        const QString url = linkAt(cell);
        if (!url.isEmpty()) {
            emit linkActivated(url);
            return;
        }
    }
    int interval = QGuiApplication::styleHints()->mouseDoubleClickInterval();
    if (m_lastClick.isValid() && m_lastClick.elapsed() < interval &&
        cell == m_lastClickCell && e->button() == m_lastClickButton)
        m_clickCount = m_clickCount % 3 + 1;
    else
        m_clickCount = 1;
    m_lastClick.start();
    m_lastClickCell = cell;
    m_lastClickButton = e->button();
    mouseButton(e, m_clickCount == 1 ? SSHLX_MA_CLICK :
                   m_clickCount == 2 ? SSHLX_MA_2CLK : SSHLX_MA_3CLK);
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *e)
{
    // Qt sends this instead of the second press; count it as a press.
    mousePressEvent(e);
}

void TerminalView::mouseReleaseEvent(QMouseEvent *e)
{
    mouseButton(e, SSHLX_MA_RELEASE);
}

void TerminalView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_session)
        return;
    QPoint cell = cellAt(e->position());
    sshlx_mbutton b = SSHLX_MB_NOTHING;
    if (e->buttons() & Qt::LeftButton) b = SSHLX_MB_LEFT;
    else if (e->buttons() & Qt::MiddleButton) b = SSHLX_MB_MIDDLE;
    else if (e->buttons() & Qt::RightButton) b = SSHLX_MB_RIGHT;
    sshlx_session_mouse(m_session, b,
                        b == SSHLX_MB_NOTHING ? SSHLX_MA_MOVE : SSHLX_MA_DRAG,
                        cell.x(), cell.y(), KeyMap::modifiers(e->modifiers()));
}

void TerminalView::wheelEvent(QWheelEvent *e)
{
    if (!m_session)
        return;
    // Ctrl+wheel zooms, as in most terminals; one step per notch, so a
    // touchpad's many small deltas add up first.
    if (e->modifiers() == Qt::ControlModifier && e->angleDelta().y()) {
        m_zoomWheel += e->angleDelta().y();
        while (qAbs(m_zoomWheel) >= 120) {
            emit fontSizeStep(m_zoomWheel > 0 ? +1 : -1);
            m_zoomWheel -= m_zoomWheel > 0 ? 120 : -120;
        }
        e->accept();
        return;
    }
    QPoint cell = cellAt(e->position());
    // GDK smooth-scroll deltas are 1.0 per notch; Qt's angleDelta is 120.
    double dx = -e->angleDelta().x() / 120.0;
    double dy = -e->angleDelta().y() / 120.0;
    if (e->inverted())
        dy = -dy;
    sshlx_session_wheel(m_session, dx, dy, cell.x(), cell.y(),
                        KeyMap::modifiers(e->modifiers()));
    e->accept();
}

void TerminalView::focusInEvent(QFocusEvent *)
{
    if (m_session)
        sshlx_session_focus(m_session, true);
}

void TerminalView::focusOutEvent(QFocusEvent *)
{
    if (m_session)
        sshlx_session_focus(m_session, false);
}
