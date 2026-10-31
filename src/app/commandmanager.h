// CommandManager - the macros as a filterable tree in a dock, one more view
// of the list the button bars show.
#pragma once

#include <QWidget>

#include "macros.h"

class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class CommandManager : public QWidget {
    Q_OBJECT
public:
    explicit CommandManager(QWidget *parent = nullptr);
    QTreeWidget *tree() const { return m_tree; }
    QLineEdit *filter() const { return m_filter; }

signals:
    void macroTriggered(const Macro &macro, bool onTargets);
    void manageRequested();

private:
    void rebuild();
    void run(QTreeWidgetItem *item, bool onTargets);

    QLineEdit *m_filter;
    QComboBox *m_bar;
    QTreeWidget *m_tree;
    QList<Macro> m_macros;
};
