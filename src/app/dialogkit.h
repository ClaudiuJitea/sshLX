// DialogKit - building blocks so every sshLX dialog looks the same: a header
// (title + subtitle), "cards" that group related settings, muted hints, and
// a footer with custom buttons (filled accent primary, quiet secondary; no
// stock icons). Also sshLX's own message dialog, replacing QMessageBox.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QBoxLayout;
class QDialogButtonBox;
class QFrame;
class QLabel;
class QLayout;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace DialogKit {

// Page header: large title with an optional muted subtitle.
QWidget *header(const QString &title, const QString &subtitle = QString());
// A rounded card with a small title, holding `content`.
QFrame *card(const QString &title, QLayout *content);
// Muted, word-wrapped explanatory text.
QLabel *hint(const QString &text);
// A checkbox-like row: the control above, a hint below it.
QWidget *withHint(QWidget *control, const QString &hint);

QPushButton *primaryButton(const QString &text);
QPushButton *secondaryButton(const QString &text);
QPushButton *dangerButton(const QString &text);
// A footer row: a top separator, then buttons right-aligned. Add buttons
// with addButton(); leftWidget (optional) sits on the left.
QWidget *footer(const QList<QPushButton *> &buttons, QWidget *leftWidget = nullptr);
// Make an existing QDialogButtonBox follow the same style.
void polish(QDialogButtonBox *box);

// Standard dialog margins/spacing for a top-level QVBoxLayout.
void frame(QVBoxLayout *layout);

// ---- message dialogs -------------------------------------------------------
enum class Kind { Info, Warning, Error, Question };

// Shows a modal message; returns the index of the chosen button (or -1 if
// closed). buttons[defaultIndex] is the primary (default) button.
int message(QWidget *parent, Kind kind, const QString &title, const QString &text,
            const QStringList &buttons, int defaultIndex = 0,
            const QString &details = QString(), bool dangerPrimary = false);

void info(QWidget *parent, const QString &title, const QString &text);
void warning(QWidget *parent, const QString &title, const QString &text);
void error(QWidget *parent, const QString &title, const QString &text);
// Yes/No question. `yes` is the label of the affirmative button; the safe
// choice is the default unless defaultYes.
bool confirm(QWidget *parent, const QString &title, const QString &text,
             const QString &yes, const QString &no = QString(),
             bool defaultYes = false, bool destructive = false);
// Non-modal variant of warning() (for background notices).
void notify(QWidget *parent, Kind kind, const QString &title, const QString &text);

} // namespace DialogKit
