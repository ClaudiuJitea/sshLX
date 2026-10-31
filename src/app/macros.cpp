#include "macros.h"

#include "dialogkit.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHash>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "icons.h"

using namespace DialogKit;

QList<Macro> loadMacros()
{
    QSettings st;
    QList<Macro> macros;
    const int n = st.beginReadArray("macros");
    for (int i = 0; i < n; i++) {
        st.setArrayIndex(i);
        macros.append({st.value("name").toString(), st.value("text").toString(),
                       st.value("folder").toString(), st.value("script").toString(),
                       st.value("bar").toString()});
    }
    st.endArray();
    return macros;
}

void saveMacros(const QList<Macro> &macros)
{
    QSettings st;
    st.remove("macros");
    st.beginWriteArray("macros", macros.size());
    for (int i = 0; i < macros.size(); i++) {
        st.setArrayIndex(i);
        st.setValue("name", macros[i].name);
        st.setValue("text", macros[i].text);
        st.setValue("folder", macros[i].folder);
        st.setValue("script", macros[i].script);
        st.setValue("bar", macros[i].bar);
    }
    st.endArray();
    st.sync();
    emit MacroEvents::instance()->changed();
}

MacroEvents *MacroEvents::instance()
{
    static MacroEvents *e = new MacroEvents;
    return e;
}

void addMacrosToMenu(QMenu *menu, const QList<Macro> &macros, bool enabled,
                     const std::function<void(const Macro &)> &run)
{
    QHash<QString, QMenu *> folders;
    const std::function<QMenu *(const QString &)> folderMenu = [&](const QString &path) -> QMenu * {
        if (path.isEmpty())
            return menu;
        if (QMenu *m = folders.value(path))
            return m;
        const int slash = int(path.lastIndexOf('/'));
        QMenu *parent = folderMenu(slash < 0 ? QString() : path.left(slash));
        QMenu *m = parent->addMenu(Icons::get("folder"), path.mid(slash + 1));
        folders.insert(path, m);
        return m;
    };
    for (const Macro &m : macros) {
        QAction *a = folderMenu(m.folder)->addAction(m.name, menu, [run, m] { run(m); });
        a->setEnabled(enabled);
    }
}

ButtonBar::ButtonBar(QWidget *parent) : QToolBar(tr("Button Bar"), parent)
{
    setObjectName("ButtonBar");
    setFloatable(false);
    setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_chosen = QSettings().value("macros/currentBar").toString();
    connect(MacroEvents::instance(), &MacroEvents::changed, this, &ButtonBar::rebuild);
    rebuild();
}

QStringList ButtonBar::barNames()
{
    QStringList names;
    for (const Macro &m : loadMacros())
        if (!m.bar.isEmpty() && !names.contains(m.bar))
            names.append(m.bar);
    names.sort(Qt::CaseInsensitive);
    names.prepend(QString());
    return names;
}

void ButtonBar::setChosenBar(const QString &name)
{
    m_chosen = name;
    m_forced = QString();     // null again: a pick overrides the tab's bar
    QSettings().setValue("macros/currentBar", name);
    rebuild();
}

void ButtonBar::setForcedBar(const QString &name)
{
    if (name.isNull() == m_forced.isNull() && name == m_forced)
        return;
    m_forced = name;
    rebuild();
}

void ButtonBar::rebuild()
{
    clear();
    const QString bar = barNames().contains(shownBar()) ? shownBar() : QString();
    QList<Macro> macros;
    for (const Macro &m : loadMacros())
        if (m.bar == bar)
            macros.append(m);
    const QStringList names = barNames();
    if (names.size() > 1) {
        auto *choice = new QComboBox;
        choice->setObjectName("barChoice");
        for (const QString &n : names)
            choice->addItem(n.isEmpty() ? tr("Default bar") : n, n);
        choice->setCurrentIndex(qMax(0, choice->findData(bar)));
        // Queued: picking rebuilds the bar, and with it this combo box.
        connect(choice, &QComboBox::activated, this, [this, choice] {
            const QString name = choice->currentData().toString();
            QTimer::singleShot(0, this, [this, name] { setChosenBar(name); });
        });
        addWidget(choice);
        addSeparator();
    }
    auto run = [this](const Macro &m) { emit macroTriggered(m, false); };
    QStringList seen;
    for (const Macro &m : macros) {
        if (m.folder.isEmpty()) {
            auto *b = new QToolButton;
            b->setText(m.name);
            b->setToolTip(m.script.isEmpty() ? m.text : m.script);
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QToolButton::clicked, this, [run, m] { run(m); });
            connect(b, &QWidget::customContextMenuRequested, this, [this, b, m](const QPoint &pos) {
                QMenu menu;
                menu.addAction(Icons::get("send"), tr("Run on Command Window Tabs"), this,
                               [this, m] { emit macroTriggered(m, true); });
                menu.addAction(tr("Manage Macros..."), this, &ButtonBar::manageRequested);
                menu.exec(b->mapToGlobal(pos));
            });
            addWidget(b);
            continue;
        }
        const QString top = m.folder.section('/', 0, 0);
        if (seen.contains(top))
            continue;
        seen.append(top);
        QList<Macro> inside;
        for (Macro x : macros) {
            if (x.folder == top || x.folder.startsWith(top + '/')) {
                x.folder = x.folder.mid(top.size() + 1);
                inside.append(x);
            }
        }
        auto *b = new QToolButton;
        b->setText(top);
        b->setIcon(Icons::get("folder"));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(b);
        addMacrosToMenu(menu, inside, true, run);
        b->setMenu(menu);
        addWidget(b);
    }
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(spacer);
    QAction *manage = addAction(macros.isEmpty() ? tr("Add a Macro...") : tr("Manage..."));
    connect(manage, &QAction::triggered, this, &ButtonBar::manageRequested);
}

MacroDialog::MacroDialog(QWidget *parent) : QDialog(parent), m_macros(loadMacros())
{
    setWindowTitle(tr("Macros"));
    resize(720, 480);
    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("Macros"), tr("Commands to run from the terminal menu or the button bar, one per line.")));

    m_list = new QListWidget;
    m_list->setMaximumWidth(220);
    for (const Macro &m : m_macros)
        m_list->addItem(m.name);
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Name"));
    m_name->setAccessibleName(tr("Macro name"));
    m_text = new QPlainTextEdit;
    m_text->setAccessibleName(tr("Commands"));
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_folder = new QComboBox;
    m_folder->setEditable(true);
    m_folder->lineEdit()->setPlaceholderText(tr("Folder (optional); use / for subfolders"));
    QStringList folders;
    for (const Macro &m : m_macros)
        if (!m.folder.isEmpty() && !folders.contains(m.folder))
            folders.append(m.folder);
    folders.sort();
    m_folder->addItem(QString());
    m_folder->addItems(folders);
    m_bar = new QComboBox;
    m_bar->setEditable(true);
    m_bar->lineEdit()->setPlaceholderText(tr("Button bar (empty: the default bar)"));
    m_bar->addItems(ButtonBar::barNames());
    m_kind = new QComboBox;
    m_kind->addItem(tr("Commands"));
    m_kind->addItem(tr("Python script"));
    m_script = new QLineEdit;
    m_script->setPlaceholderText(tr("Path of the script"));
    m_browse = secondaryButton(tr("Browse..."));
    auto *scriptRow = new QHBoxLayout;
    scriptRow->addWidget(m_script, 1);
    scriptRow->addWidget(m_browse);
    auto *right = new QVBoxLayout;
    right->addWidget(m_name);
    right->addWidget(m_folder);
    right->addWidget(m_bar);
    right->addWidget(m_kind);
    right->addLayout(scriptRow);
    right->addWidget(m_text, 1);
    auto *row = new QHBoxLayout;
    row->addWidget(m_list);
    row->addLayout(right, 1);
    v->addLayout(row, 1);

    auto *buttons = new QWidget;
    auto *bh = new QHBoxLayout(buttons);
    bh->setContentsMargins(0, 0, 0, 0);
    auto *add = secondaryButton(tr("Add"));
    auto *remove = secondaryButton(tr("Remove"));
    bh->addWidget(add);
    bh->addWidget(remove);
    QPushButton *cancel = secondaryButton(tr("Cancel"));
    QPushButton *save = primaryButton(tr("Save"));
    v->addWidget(footer({cancel, save}, buttons));

    connect(m_kind, &QComboBox::currentIndexChanged, this, &MacroDialog::updateKind);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Python Script"), m_script->text(),
                                                          tr("Python scripts (*.py);;All files (*)"));
        if (!path.isEmpty())
            m_script->setText(path);
    });
    connect(m_list, &QListWidget::currentRowChanged, this, &MacroDialog::select);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (m_row >= 0)
            m_list->item(m_row)->setText(t);
    });
    connect(add, &QPushButton::clicked, this, [this] {
        store();
        m_macros.append({tr("New Macro"), QString(), m_folder->currentText().trimmed(), QString(),
                          m_bar->currentText().trimmed()});
        m_list->addItem(m_macros.last().name);
        m_list->setCurrentRow(m_macros.size() - 1);
        m_name->setFocus();
        m_name->selectAll();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_row;
        if (row < 0)
            return;
        m_row = -1;     // what is in the editors must not land on a neighbour
        m_macros.removeAt(row);
        delete m_list->takeItem(row);
        select(m_list->currentRow());
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] {
        store();
        for (const Macro &m : m_macros) {
            if (m.name.trimmed().isEmpty()) {
                error(this, tr("Macros"), tr("Every macro needs a name."));
                return;
            }
        }
        saveMacros(m_macros);
        accept();
    });
    m_list->setCurrentRow(m_macros.isEmpty() ? -1 : 0);
    select(m_list->currentRow());
}

void MacroDialog::store()
{
    if (m_row >= 0 && m_row < m_macros.size())
        m_macros[m_row] = {m_name->text(), m_text->toPlainText(),
                           m_folder->currentText().trimmed().remove(QRegularExpression("^/+|/+$")),
                           m_kind->currentIndex() == 1 ? m_script->text().trimmed() : QString(),
                           m_bar->currentText().trimmed()};
}

void MacroDialog::select(int row)
{
    store();
    m_row = row;
    const bool have = row >= 0 && row < m_macros.size();
    m_name->setEnabled(have);
    m_text->setEnabled(have);
    m_name->setText(have ? m_macros[row].name : QString());
    m_text->setPlainText(have ? m_macros[row].text : QString());
    m_folder->setEnabled(have);
    m_kind->setEnabled(have);
    m_folder->setCurrentText(have ? m_macros[row].folder : QString());
    m_bar->setEnabled(have);
    m_bar->setCurrentText(have ? m_macros[row].bar : QString());
    m_script->setText(have ? m_macros[row].script : QString());
    m_kind->setCurrentIndex(have && !m_macros[row].script.isEmpty() ? 1 : 0);
    updateKind();
}

void MacroDialog::updateKind()
{
    const bool script = m_kind->currentIndex() == 1;
    m_text->setVisible(!script);
    m_script->setVisible(script);
    m_browse->setVisible(script);
}
