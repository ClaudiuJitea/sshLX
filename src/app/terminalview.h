// TerminalView - draws a PuTTY terminal with QPainter and feeds keyboard,
// mouse and input-method events to the engine.
//
// Rendering follows GTK PuTTY's model: the engine draws runs of text into a
// backing image (at the widget's device pixel ratio) and paintEvent() blits
// it. Glyphs are placed cell by cell so the grid stays exact even when
// font fallback supplies glyphs with different advances.
#pragma once

#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QVector>
#include <QWidget>

#include <memory>

#include "sshlx_engine.h"

struct TerminalFonts {
    QString normal, bold, wide, wideBold;   // PuTTY font names
    bool shadowBold = false;
    int shadowBoldOffset = 1;
    int border = 1;
};

struct KeyBinding;

class TerminalView : public QWidget {
    Q_OBJECT
public:
    explicit TerminalView(QWidget *parent = nullptr);
    ~TerminalView() override;

    void setSession(sshlx_session *s) { m_session = s; }
    void setFonts(const TerminalFonts &f);
    const TerminalFonts &fonts() const { return m_fontNames; }
    QSize cellSize() const { return QSize(m_cw, m_ch); }
    QSize sizeForGrid(int cols, int rows) const;
    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    void setDefaultBackground(QColor c);
    void setRawMousePointer(bool raw);
    void setBusy(bool busy);
    void setCursorCell(int col, int row);
    void setInitialGrid(int cols, int rows) { m_hintCols = cols; m_hintRows = rows; }
    bool usedFallbackFont() const { return m_fallbackFont; }
    // Keys are dropped while a config push owns the session.
    void setInputBlocked(bool blocked) { m_inputBlocked = blocked; }
    // A locked tab, separate from the blocks that come and go by themselves.
    void setLocked(bool locked) { m_locked = locked; }
    // The http(s) link drawn at a cell, or empty.
    QString linkAt(const QPoint &cell) const;
    // Keyword colours, or null for none. The set must outlive the view;
    // call this again whenever it changes.
    void setHighlights(const class HighlightSet *set);
    // Capturing: the engine's drawing only updates rowText(), not the picture.
    // Find in Terminal reads the scrollback a page at a time this way.
    void setCapturing(bool on) { m_capturing = on; }
    QString rowText(int row) const;
    int cursorRow() const { return m_cursorRow; }
    // Lights up every match of text; empty clears. The current match is
    // start..start+len (UTF-16 units of rowText) on a screen row, -1 for none.
    void setFind(const QString &text, bool matchCase);
    void setFindCurrent(int row, int start, int len);

    // Engine draw callbacks (forwarded by SessionWidget).
    bool beginDraw();
    void drawText(int col, int row, int ncells, const uint32_t *text, int len,
                  const sshlx_text_style *st);
    void drawCursorShape(int col, int row, int ncells, int shape, bool passive,
                         bool rightEdge, sshlx_rgb colour, int lineAttr);
    void drawTrustSigil(int col, int row);
    void endDraw();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // A key or typed text went to the session.
    void keySent(const sshlx_key_event *ev, int qtKey);
    void textSent(const QString &text);
    // A key mapping was pressed, in place of sending the key.
    void keyBound(const KeyBinding &binding);
    void filesDropped(const QStringList &paths);
    void gridResized(int cols, int rows);
    // A right click the terminal did not use, or the Menu key (as Ctrl).
    void rightClicked(const QPoint &globalPos, const QPoint &cell, bool ctrl);
    void linkActivated(const QString &url);
    void fontSizeStep(int delta);
    void keyPressedWhileDisconnected();

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void inputMethodEvent(QInputMethodEvent *) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool event(QEvent *) override;
    bool focusNextPrevChild(bool) override { return false; }

private:
    void recomputeMetrics();
    void paintHighlights(QPainter &p, const QRect &area);
    void matchRow(int row);
    bool overlayActive() const;
    void recreateImage();
    void updateGrid();
    QPoint cellAt(const QPointF &pos) const;
    const QFont &pickFont(bool bold, bool wide) const;
    void drawGlyphs(QPainter &p, qreal x, qreal baseline, const uint32_t *text,
                    int len, bool combining, int cellsPerChar, bool exactAdvance);
    void mouseButton(QMouseEvent *e, sshlx_maction action);

    sshlx_session *m_session = nullptr;
    TerminalFonts m_fontNames;
    QFont m_font, m_boldFont, m_wideFont, m_wideBoldFont;
    bool m_haveBold = false, m_haveWide = false, m_haveWideBold = false;
    bool m_exactAdvance = false, m_fallbackFont = false;
    int m_cw = 8, m_ch = 16, m_ascent = 12, m_strikeY = 8;
    int m_cols = 0, m_rows = 0;
    int m_hintCols = 80, m_hintRows = 24;
    QImage m_image;
    std::unique_ptr<QPainter> m_painter;
    QRect m_dirty;
    // What is on screen, for links and keywords: m_cols by m_rows.
    struct Cell {
        char32_t ch = U' ';     // 0 = right half of a wide char
        QRgb fg = 0, bg = 0;
        bool bold = false;
        bool plain = false;     // simple enough to draw again over the image
    };
    QVector<Cell> m_cells;
    const class HighlightSet *m_highlights = nullptr;
    QVector<QVector<int>> m_rowRules;  // per row, per column: rule index or -1
    QVector<bool> m_rowStale;
    std::unique_ptr<class HighlightSet> m_find;
    int m_findRow = -1, m_findStart = 0, m_findLen = 0;
    bool m_capturing = false;
    bool m_inputBlocked = false, m_locked = false;
    QColor m_defaultBg = Qt::black;
    int m_cursorCol = 0, m_cursorRow = 0;
    bool m_rawPointer = false, m_busy = false;
    // click counting for double/triple click
    QElapsedTimer m_lastClick;
    QPoint m_lastClickCell;
    int m_clickCount = 0;
    Qt::MouseButton m_lastClickButton = Qt::NoButton;
    QByteArray m_keyText;
    int m_zoomWheel = 0;   // Ctrl+wheel angle not yet turned into a font step
};
