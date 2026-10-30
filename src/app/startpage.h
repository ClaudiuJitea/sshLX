// StartPage - shown in the session area: anchors the document area (so the
// side panels keep their width) and offers the common first actions.
#pragma once

#include <QIcon>
#include <QList>
#include <QPair>
#include <QWidget>

class QLabel;
class QVBoxLayout;

class StartPage : public QWidget {
    Q_OBJECT
public:
    explicit StartPage(QWidget *parent = nullptr);

    struct Recent {
        QString id;       // connection id
        QString name;
        QString detail;   // host, shown muted on the right
        QIcon icon;
    };
    // The "Recent connections" list, most recent first.
    void setRecent(const QList<Recent> &recent);

signals:
    void newConnection();
    void quickConnect();
    void localShell();
    void options();
    void openRecent(const QString &connectionId);

protected:
    void changeEvent(QEvent *e) override;

private:
    void refreshIcons();

    QLabel *m_logo;
    QList<QPair<QLabel *, QString>> m_icons;   // icon label, icon name
    QVBoxLayout *m_recent;
};
