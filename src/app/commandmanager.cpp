#include "commandmanager.h"

#include <QComboBox>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "icons.h"

CommandManager::CommandManager(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter commands..."));
    m_filter->setClearButtonEnabled(true);
    m_bar = new QComboBox;
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setAccessibleName(tr("Commands"));
    m_filter->setAccessibleName(tr("Filter commands"));
    m_bar->setAccessibleName(tr("Button bar"));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *manage = DialogKit::secondaryButton(tr("Manage Macros..."));
    v->addWidget(m_filter);
    v->addWidget(m_bar);
    v->addWidget(m_tree, 1);
    v->addWidget(manage);
    connect(manage, &QPushButton::clicked, this, &CommandManager::manageRequested);
    connect(m_filter, &QLineEdit::textChanged, this, &CommandManager::rebuild);
    connect(m_bar, &QComboBox::activated, this, &CommandManager::rebuild);
    connect(MacroEvents::instance(), &MacroEvents::changed, this, &CommandManager::rebuild);
    // Double click, or Enter.
    connect(m_tree, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem *it) { run(it, false); });
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *it = m_tree->itemAt(pos);
        if (!it || it->data(0, Qt::UserRole).toInt() < 0)
            return;
        QMenu menu;
        menu.addAction(tr("Run"), this, [this, it] { run(it, false); });
        menu.addAction(Icons::get("send"), tr("Run on Command Window Tabs"), this,
                       [this, it] { run(it, true); });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });
    rebuild();
}

void CommandManager::run(QTreeWidgetItem *item, bool onTargets)
{
    const int i = item->data(0, Qt::UserRole).toInt();
    if (i >= 0 && i < m_macros.size())
        emit macroTriggered(m_macros[i], onTargets);
}

void CommandManager::rebuild()
{
    const QVariant wasBar = m_bar->currentData();
    m_bar->clear();
    m_bar->addItem(tr("All bars"), QVariant());
    for (const QString &n : ButtonBar::barNames())
        m_bar->addItem(n.isEmpty() ? tr("Default bar") : n, n);
    m_bar->setVisible(m_bar->count() > 2);
    const int at = wasBar.isValid() ? m_bar->findData(wasBar) : 0;
    m_bar->setCurrentIndex(qMax(0, at));
    const QVariant bar = m_bar->currentData();

    m_tree->clear();
    m_macros = loadMacros();
    const QString filter = m_filter->text().trimmed();
    QHash<QString, QTreeWidgetItem *> folders;
    const std::function<QTreeWidgetItem *(const QString &)> folder =
        [&](const QString &path) -> QTreeWidgetItem * {
        if (path.isEmpty())
            return m_tree->invisibleRootItem();
        if (QTreeWidgetItem *f = folders.value(path))
            return f;
        const int slash = int(path.lastIndexOf('/'));
        auto *f = new QTreeWidgetItem(folder(slash < 0 ? QString() : path.left(slash)),
                                      {path.mid(slash + 1)});
        f->setIcon(0, Icons::get("folder"));
        f->setData(0, Qt::UserRole, -1);
        folders.insert(path, f);
        return f;
    };
    for (int i = 0; i < m_macros.size(); i++) {
        const Macro &m = m_macros[i];
        if (bar.isValid() && m.bar != bar.toString())
            continue;
        const QString hay = m.name + '\n' + m.text + '\n' + m.folder + '\n' + m.script;
        if (!filter.isEmpty() && !hay.contains(filter, Qt::CaseInsensitive))
            continue;
        auto *it = new QTreeWidgetItem(folder(m.folder), {m.name});
        it->setData(0, Qt::UserRole, i);
        it->setIcon(0, Icons::get(m.script.isEmpty() ? "terminal" : "script"));
        it->setToolTip(0, m.script.isEmpty() ? m.text : m.script);
    }
    m_tree->expandAll();
}
