// StartPage - shown in the session area: anchors the document area (so the
// side panels keep their width) and offers the common first actions.
#pragma once

#include <QWidget>

class StartPage : public QWidget {
    Q_OBJECT
public:
    explicit StartPage(QWidget *parent = nullptr);

signals:
    void newConnection();
    void quickConnect();
    void importSessions();
    void options();
};
