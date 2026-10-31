// OptionsDialog - Tools > Options: behaviour, serial detection, keyboard
// shortcuts (so they never steal keys a terminal needs) and the opt-in
// keyring.
#pragma once

#include <QDialog>

#include "highlightrules.h"

class MainWindow;
class QListWidget;

// Apply the interface font size (Options; 0 = automatic: the desktop font,
// at least 11 pt). Call once at startup, after QApplication exists.
void applyUiFontSize();
class QCheckBox;
class QLineEdit;
class QTableWidget;
class QDoubleSpinBox;

class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit OptionsDialog(MainWindow *main);

private:
    void apply();
    QWidget *buildShortcuts();
    QWidget *buildSerial();
    QWidget *buildHighlighting();
    void refreshKeywords();

    MainWindow *m_main;
    QCheckBox *m_restore, *m_reuseTab, *m_closeOnExit, *m_udev, *m_desktop, *m_keyring;
    QCheckBox *m_copyOnSelect, *m_confirmPaste, *m_zmodemAuto;
    QLineEdit *m_python;
    class QSpinBox *m_rotateMB, *m_rotateHours, *m_rotateKeep;
    class QComboBox *m_rightClick;
    QListWidget *m_keywordList;
    QCheckBox *m_keywordsOn;
    QList<HighlightRule> m_keywords;
    QTableWidget *m_shortcuts, *m_rules;
    QDoubleSpinBox *m_fontSize;
    class QComboBox *m_theme;
};
