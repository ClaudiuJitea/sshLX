// StartPage - shown in the session area: anchors the document area (so the
// side panels keep their width) and offers the common first actions.
#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

class QHBoxLayout;
class QLabel;

class StartPage : public QWidget {
    Q_OBJECT
public:
    explicit StartPage(QWidget *parent = nullptr);
    // The shortcut row shows the keys currently assigned in Options.
    void refreshShortcuts();

signals:
    void newConnection();
    void quickConnect();
    void importSessions();
    void options();

protected:
    void showEvent(QShowEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void refreshIcons();

    QLabel *m_logo;
    QList<QPair<QLabel *, QString>> m_icons;   // icon label, icon name
    QHBoxLayout *m_keys;
};
