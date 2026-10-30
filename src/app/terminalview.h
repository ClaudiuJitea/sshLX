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
    void gridResized(int cols, int rows);
    void contextMenuRequested(const QPoint &globalPos);
    void fontSizeStep(int delta);
    void keyPressedWhileDisconnected();

protected:
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
