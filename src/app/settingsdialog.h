// SettingsDialog - a generic Qt rendering of PuTTY's own configuration
// dialog. The control box is built by PuTTY's config.c (+ unix additions)
// inside the engine; this class lays it out (category tree + panels, PuTTY's
// column layout) and routes every widget change through PuTTY's handlers
// via the sshlx_dlg_* bridge. No option is hand-written, so every PuTTY
// option is reachable.
#pragma once

#include <QDialog>
#include <QHash>
#include <QPointer>

#include "sshlx_engine.h"

class QLabel;
class QLineEdit;
class QComboBox;
class QButtonGroup;
class QCheckBox;
class QPushButton;
class QTreeWidget;
class QStackedWidget;
class QGridLayout;
class QHBoxLayout;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // For the main config box. conf is edited in place; with
    // takeOwnership the dialog frees it on destruction.
    SettingsDialog(sshlx_conf *conf, bool midsession, int protcfginfo,
                   const QString &title, QWidget *parent = nullptr,
                   bool takeOwnership = false);
    // For PuTTY's host CA configuration box.
    static SettingsDialog *createCaConfig(QWidget *parent);
    ~SettingsDialog() override;

    sshlx_conf *conf() const { return m_conf; }
    // Select the panel with this PuTTY path, e.g. "Connection/SSH".
    void showPanel(const QString &path);
    // Add a button next to PuTTY's own (Open/Cancel) that closes the
    // dialog with the given result code.
    QPushButton *addActionButton(const QString &text, int resultCode);

signals:
    void aboutRequested();

private:
    struct Ctrl {
        sshlx_ctrl_type type;
        QLabel *label = nullptr;
        QLineEdit *edit = nullptr;
        QComboBox *combo = nullptr;
        QButtonGroup *radios = nullptr;
        QCheckBox *check = nullptr;
        QPushButton *button = nullptr;
        QTreeWidget *list = nullptr;
        QWidget *focusWidget = nullptr;
        int listHeight = 0;
        bool multisel = false;
        QString labelText;
        char shortcut = 0;
    };

    SettingsDialog(QWidget *parent);
    void build(const QString &title);
    QWidget *buildControl(const sshlx_ctrl_desc &d);
    void refreshCtrl(int id);
    void refreshAll();
    static QString mnemonic(const QString &label, char shortcut);

    static void cbChanged(void *ctx, int id);
    static void cbLabel(void *ctx, int id, const char *label);
    static void cbFocus(void *ctx, int id);
    static void cbSelectRange(void *ctx, int id, size_t start, size_t len);
    static void cbBeep(void *ctx);
    static void cbError(void *ctx, const char *msg);
    static void cbEnd(void *ctx, int value);
    static void cbColour(void *ctx, int id, int r, int g, int b);
    static void cbCaConfig(void *ctx);
    static void cbAbout(void *ctx);
    sshlx_dlg_callbacks callbacks();

    sshlx_dlg *m_dlg = nullptr;
    sshlx_conf *m_conf = nullptr;
    bool m_ownConf = false;
    bool m_updating = false;
    QHash<int, Ctrl> m_ctrls;
    QHash<QWidget *, int> m_widgetIds;
    QTreeWidget *m_tree = nullptr;
    QStackedWidget *m_stack = nullptr;
    QHBoxLayout *m_buttonRow = nullptr;
    QHash<QString, int> m_pageForPath;
    QPointer<SettingsDialog> m_caDialog;
};
