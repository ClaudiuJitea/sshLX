#include "settingsdialog.h"
#include "dialogkit.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "fontspec.h"

// ---------------------------------------------------------------------------
sshlx_dlg_callbacks SettingsDialog::callbacks()
{
    sshlx_dlg_callbacks cb{};
    cb.ctx = this;
    cb.ctrl_changed = &SettingsDialog::cbChanged;
    cb.label_changed = &SettingsDialog::cbLabel;
    cb.set_focus = &SettingsDialog::cbFocus;
    cb.select_range = &SettingsDialog::cbSelectRange;
    cb.beep = &SettingsDialog::cbBeep;
    cb.error = &SettingsDialog::cbError;
    cb.end = &SettingsDialog::cbEnd;
    cb.colour_select = &SettingsDialog::cbColour;
    cb.open_ca_config = &SettingsDialog::cbCaConfig;
    cb.about = &SettingsDialog::cbAbout;
    return cb;
}

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {}

SettingsDialog::SettingsDialog(sshlx_conf *conf, bool midsession,
                               int protcfginfo, const QString &title,
                               QWidget *parent, bool takeOwnership)
    : QDialog(parent), m_conf(conf), m_ownConf(takeOwnership)
{
    sshlx_dlg_callbacks cb = callbacks();
    m_dlg = sshlx_dlg_new_config(conf, midsession, protcfginfo, &cb);
    build(title);
}

SettingsDialog *SettingsDialog::createCaConfig(QWidget *parent)
{
    auto *d = new SettingsDialog(parent);
    sshlx_dlg_callbacks cb = d->callbacks();
    d->m_dlg = sshlx_dlg_new_ca_config(&cb);
    d->build(tr("PuTTY trusted host certification authorities"));
    return d;
}

SettingsDialog::~SettingsDialog()
{
    // Widgets go first so no signal reaches a freed dialog model.
    const auto children = findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : children)
        delete w;
    sshlx_dlg_free(m_dlg);
    if (m_ownConf)
        sshlx_conf_free(m_conf);
}

QString SettingsDialog::mnemonic(const QString &label, char shortcut)
{
    QString l = label;
    l.replace('&', "&&");
    if (!shortcut)
        return l;
    int i = l.indexOf(QChar(shortcut), 0, Qt::CaseInsensitive);
    if (i >= 0)
        l.insert(i, '&');
    return l;
}

void SettingsDialog::build(const QString &title)
{
    setWindowTitle(title);
    const sshlx_ctrl_set *sets;
    size_t nsets = sshlx_dlg_sets(m_dlg, &sets);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    // Footer: PuTTY's global buttons (Open/Cancel/About/...), styled.
    auto *footer = new QWidget;
    footer->setObjectName("DialogFooter");
    QHBoxLayout *globalRow = new QHBoxLayout(footer);
    globalRow->setContentsMargins(20, 12, 20, 14);
    globalRow->setSpacing(8);
    globalRow->addStretch(1);
    m_buttonRow = globalRow;
    bool tree = false;
    for (size_t i = 0; i < nsets; i++)
        if (strchr(sets[i].path, '/') || (*sets[i].path && i > 0))
            tree = true;

    QWidget *panelsHost = nullptr;
    if (tree) {
        auto *split = new QSplitter;
        split->setChildrenCollapsible(false);
        auto *navPane = new QWidget;
        navPane->setObjectName("NavPane");
        auto *nv = new QVBoxLayout(navPane);
        nv->setContentsMargins(10, 14, 6, 10);
        m_tree = new QTreeWidget;
        m_tree->setObjectName("Nav");
        m_tree->setHeaderHidden(true);
        m_tree->setIndentation(14);
        m_tree->setFocusPolicy(Qt::NoFocus);
        nv->addWidget(m_tree);
        navPane->setMinimumWidth(200);
        m_stack = new QStackedWidget;
        split->addWidget(navPane);
        split->addWidget(m_stack);
        split->setStretchFactor(1, 1);
        split->setSizes({220, 640});
        outer->addWidget(split, 1);
        panelsHost = m_stack;
    } else {
        panelsHost = new QWidget;
        new QVBoxLayout(panelsHost);
        outer->addWidget(panelsHost, 1);
    }

    QHash<QString, QTreeWidgetItem *> treeItems;
    QHash<QString, QVBoxLayout *> pageLayouts;

    for (size_t si = 0; si < nsets; si++) {
        const sshlx_ctrl_set &set = sets[si];
        const QString path = QString::fromUtf8(set.path);

        QVBoxLayout *pageLayout = nullptr;
        if (!path.isEmpty()) {
            if (!pageLayouts.contains(path)) {
                QWidget *page = new QWidget;
                auto *pl = new QVBoxLayout(page);
                pl->setContentsMargins(22, 18, 22, 14);
                pl->setSpacing(12);
                pl->setAlignment(Qt::AlignTop);
                pageLayouts[path] = pl;
                if (m_stack) {
                    auto *scroll = new QScrollArea;
                    scroll->setWidgetResizable(true);
                    scroll->setFrameShape(QFrame::NoFrame);
                    scroll->setWidget(page);
                    m_pageForPath[path] = m_stack->addWidget(scroll);
                    // Tree item, parents first.
                    QStringList parts = path.split('/');
                    QString acc;
                    QTreeWidgetItem *parent = nullptr;
                    for (const QString &part : parts) {
                        acc = acc.isEmpty() ? part : acc + '/' + part;
                        if (!treeItems.contains(acc)) {
                            auto *it = parent ? new QTreeWidgetItem(parent)
                                              : new QTreeWidgetItem(m_tree);
                            it->setText(0, part);
                            it->setData(0, Qt::UserRole, acc);
                            treeItems[acc] = it;
                        }
                        parent = treeItems[acc];
                    }
                } else {
                    panelsHost->layout()->addWidget(page);
                }
            }
            pageLayout = pageLayouts[path];
        }

        if (set.panel_title) {
            if (pageLayout) {
                pageLayout->addWidget(DialogKit::header(QString::fromUtf8(set.panel_title)));
            }
            continue;
        }

        // Global action buttons (Open/Cancel/About/Help...).
        if (path.isEmpty()) {
            for (size_t ci = 0; ci < set.n_ctrls; ci++) {
                const sshlx_ctrl_desc &d = set.ctrls[ci];
                if (d.type == SSHLX_C_COLUMNS)
                    continue;
                QWidget *w = buildControl(d);
                if (w)
                    globalRow->addWidget(w);
            }
            continue;
        }

        QWidget *box;
        QGridLayout *grid;
        if (set.title) {
            auto *gb = new QGroupBox(QString::fromUtf8(set.title));
            box = gb;
        } else {
            box = new QWidget;
        }
        grid = new QGridLayout(box);
        if (!set.title)
            grid->setContentsMargins(0, 0, 0, 0);

        // PuTTY's column layout: COLUMNS controls switch the number of
        // columns; controls stack independently within each column, and
        // spanning controls start below every column they cover.
        int ncols = 1;
        QVector<int> nextRow(1, 0);
        int baseRow = 0;
        auto setColumns = [&](int n, const int *percents) {
            int maxRow = baseRow;
            for (int r : nextRow)
                maxRow = std::max(maxRow, r);
            baseRow = maxRow;
            ncols = std::max(1, n);
            nextRow = QVector<int>(ncols, baseRow);
            for (int c = 0; c < ncols; c++)
                grid->setColumnStretch(c, percents ? percents[c] : 100 / ncols);
        };
        setColumns(1, nullptr);
        for (size_t ci = 0; ci < set.n_ctrls; ci++) {
            const sshlx_ctrl_desc &d = set.ctrls[ci];
            if (d.type == SSHLX_C_COLUMNS) {
                setColumns(d.ncolumns, d.column_percents);
                continue;
            }
            QWidget *w = buildControl(d);
            if (!w)
                continue;
            int col = std::min(d.column, ncols - 1);
            int span = std::max(1, std::min(d.column_span, ncols - col));
            int row = 0;
            for (int c = col; c < col + span; c++)
                row = std::max(row, nextRow[c]);
            grid->addWidget(w, row, col, 1, span);
            for (int c = col; c < col + span; c++)
                nextRow[c] = row + 1;
        }
        pageLayout->addWidget(box);
    }
    for (auto *pl : pageLayouts)
        pl->addStretch(1);
    outer->addWidget(footer);

    if (m_tree) {
        m_tree->expandAll();
        connect(m_tree, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem *cur) {
            if (!cur)
                return;
            QString path = cur->data(0, Qt::UserRole).toString();
            if (m_pageForPath.contains(path)) {
                m_stack->setCurrentIndex(m_pageForPath[path]);
                sshlx_dlg_set_visible_path(m_dlg, path.toUtf8().constData());
            }
        });
        if (m_tree->topLevelItemCount())
            m_tree->setCurrentItem(m_tree->topLevelItem(0));
        resize(900, 620);
    }

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        while (now && !m_widgetIds.contains(now))
            now = now->parentWidget();
        if (now)
            sshlx_dlg_focus_changed(m_dlg, m_widgetIds.value(now));
    });

    sshlx_dlg_refresh_all(m_dlg);
    refreshAll();
}

QPushButton *SettingsDialog::addActionButton(const QString &text, int resultCode)
{
    QPushButton *b = DialogKit::secondaryButton(text);
    m_buttonRow->insertWidget(1, b);
    connect(b, &QPushButton::clicked, this, [this, resultCode] { done(resultCode); });
    return b;
}

void SettingsDialog::showPanel(const QString &path)
{
    if (!m_tree)
        return;
    QList<QTreeWidgetItem *> all = m_tree->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive);
    for (QTreeWidgetItem *it : all)
        if (it->data(0, Qt::UserRole).toString() == path)
            m_tree->setCurrentItem(it);
}

QWidget *SettingsDialog::buildControl(const sshlx_ctrl_desc &d)
{
    Ctrl c;
    c.type = d.type;
    c.labelText = QString::fromUtf8(d.label ? d.label : "");
    c.shortcut = d.shortcut;
    const int id = d.id;
    QWidget *container = nullptr;

    switch (d.type) {
    case SSHLX_C_TEXT: {
        c.label = new QLabel(c.labelText);
        c.label->setWordWrap(true);
        container = c.label;
        break;
    }
    case SSHLX_C_CHECKBOX: {
        c.check = new QCheckBox(mnemonic(c.labelText, d.shortcut));
        connect(c.check, &QCheckBox::clicked, this, [this, id](bool on) {
            if (!m_updating)
                sshlx_dlg_set_int(m_dlg, id, on);
        });
        container = c.focusWidget = c.check;
        break;
    }
    case SSHLX_C_BUTTON: {
        c.button = d.is_default ? DialogKit::primaryButton(mnemonic(c.labelText, d.shortcut))
                                : DialogKit::secondaryButton(mnemonic(c.labelText, d.shortcut));
        c.button->setDefault(d.is_default);
        connect(c.button, &QPushButton::clicked, this, [this, id] {
            sshlx_dlg_activate(m_dlg, id);
        });
        container = c.focusWidget = c.button;
        break;
    }
    case SSHLX_C_EDITBOX: {
        container = new QWidget;
        bool above = d.percent_width == 100;
        QBoxLayout *l = above ? static_cast<QBoxLayout *>(new QVBoxLayout(container))
                              : static_cast<QBoxLayout *>(new QHBoxLayout(container));
        l->setContentsMargins(0, 0, 0, 0);
        if (!c.labelText.isEmpty()) {
            c.label = new QLabel(mnemonic(c.labelText, d.shortcut));
            l->addWidget(c.label, above ? 0 : 100 - d.percent_width);
        }
        if (d.has_list) {
            c.combo = new QComboBox;
            c.combo->setEditable(true);
            c.combo->setInsertPolicy(QComboBox::NoInsert);
            c.edit = c.combo->lineEdit();
            connect(c.combo, &QComboBox::activated, this, [this, id](int i) {
                if (!m_updating && i >= 0)
                    sshlx_dlg_list_select(m_dlg, id, size_t(i), true, true);
            });
            l->addWidget(c.combo, above ? 0 : d.percent_width);
            c.focusWidget = c.combo;
        } else {
            c.edit = new QLineEdit;
            if (d.password)
                c.edit->setEchoMode(QLineEdit::Password);
            l->addWidget(c.edit, above ? 0 : d.percent_width);
            c.focusWidget = c.edit;
        }
        connect(c.edit, &QLineEdit::textEdited, this, [this, id](const QString &t) {
            if (!m_updating)
                sshlx_dlg_set_text(m_dlg, id, t.toUtf8().constData());
        });
        if (c.label)
            c.label->setBuddy(c.focusWidget);
        break;
    }
    case SSHLX_C_RADIO: {
        container = new QWidget;
        auto *g = new QGridLayout(container);
        g->setContentsMargins(0, 0, 0, 0);
        int row = 0;
        if (!c.labelText.isEmpty()) {
            c.label = new QLabel(mnemonic(c.labelText, d.shortcut));
            g->addWidget(c.label, row++, 0, 1, std::max(1, d.radio_ncolumns));
        }
        c.radios = new QButtonGroup(container);
        int nc = std::max(1, d.radio_ncolumns);
        for (int i = 0; i < d.nbuttons; i++) {
            auto *rbtn = new QRadioButton(QString::fromUtf8(d.buttons[i]));
            c.radios->addButton(rbtn, i);
            g->addWidget(rbtn, row + i / nc, i % nc);
            if (i == 0)
                c.focusWidget = rbtn;
        }
        if (c.label && c.focusWidget)
            c.label->setBuddy(c.focusWidget);
        connect(c.radios, &QButtonGroup::idClicked, this, [this, id](int which) {
            if (!m_updating)
                sshlx_dlg_set_int(m_dlg, id, which);
        });
        break;
    }
    case SSHLX_C_LISTBOX: {
        container = new QWidget;
        c.listHeight = d.list_height;
        c.multisel = d.multisel;
        if (d.list_height == 0) {
            // drop-down list
            auto *l = new QHBoxLayout(container);
            l->setContentsMargins(0, 0, 0, 0);
            if (!c.labelText.isEmpty()) {
                c.label = new QLabel(mnemonic(c.labelText, d.shortcut));
                l->addWidget(c.label, 100 - d.percent_width);
            }
            c.combo = new QComboBox;
            l->addWidget(c.combo, d.percent_width ? d.percent_width : 100);
            connect(c.combo, &QComboBox::activated, this, [this, id](int i) {
                if (!m_updating && i >= 0)
                    sshlx_dlg_list_select(m_dlg, id, size_t(i), true, true);
            });
            c.focusWidget = c.combo;
        } else {
            auto *v = new QVBoxLayout(container);
            v->setContentsMargins(0, 0, 0, 0);
            if (!c.labelText.isEmpty()) {
                c.label = new QLabel(mnemonic(c.labelText, d.shortcut));
                v->addWidget(c.label);
            }
            auto *row = new QHBoxLayout;
            c.list = new QTreeWidget;
            c.list->setRootIsDecorated(false);
            int ncols = std::max(1, d.list_ncols);
            c.list->setColumnCount(ncols);
            c.list->setHeaderHidden(true);
            c.list->setSelectionMode(d.multisel ? QAbstractItemView::ExtendedSelection
                                                : QAbstractItemView::SingleSelection);
            int lineH = c.list->fontMetrics().height() + 4;
            c.list->setMinimumHeight(lineH * std::max(3, d.list_height));
            row->addWidget(c.list, 1);
            if (d.draglist) {
                auto *bv = new QVBoxLayout;
                auto *up = new QPushButton(tr("Up"));
                auto *down = new QPushButton(tr("Down"));
                bv->addWidget(up);
                bv->addWidget(down);
                bv->addStretch(1);
                row->addLayout(bv);
                auto move = [this, id](int dir) {
                    Ctrl &cc = m_ctrls[id];
                    int i = cc.list->indexOfTopLevelItem(cc.list->currentItem());
                    if (i < 0)
                        return;
                    sshlx_dlg_list_move(m_dlg, id, size_t(i), dir);
                    int j = i + dir;
                    if (j >= 0 && j < cc.list->topLevelItemCount())
                        cc.list->setCurrentItem(cc.list->topLevelItem(j));
                };
                connect(up, &QPushButton::clicked, this, [move] { move(-1); });
                connect(down, &QPushButton::clicked, this, [move] { move(+1); });
            }
            v->addLayout(row);
            connect(c.list, &QTreeWidget::itemSelectionChanged, this, [this, id] {
                if (m_updating)
                    return;
                Ctrl &cc = m_ctrls[id];
                for (int i = 0; i < cc.list->topLevelItemCount(); i++) {
                    bool sel = cc.list->topLevelItem(i)->isSelected();
                    if (sel != sshlx_dlg_list_selected(m_dlg, id, size_t(i)))
                        sshlx_dlg_list_select(m_dlg, id, size_t(i), sel, !cc.multisel);
                }
            });
            connect(c.list, &QTreeWidget::itemDoubleClicked, this, [this, id] {
                sshlx_dlg_activate(m_dlg, id);
            });
            c.focusWidget = c.list;
        }
        if (c.label)
            c.label->setBuddy(c.focusWidget);
        break;
    }
    case SSHLX_C_FILESELECT:
    case SSHLX_C_FONTSELECT: {
        container = new QWidget;
        auto *v = new QVBoxLayout(container);
        v->setContentsMargins(0, 0, 0, 0);
        if (!c.labelText.isEmpty()) {
            c.label = new QLabel(mnemonic(c.labelText, d.shortcut));
            v->addWidget(c.label);
        }
        auto *row = new QHBoxLayout;
        c.edit = new QLineEdit;
        row->addWidget(c.edit, 1);
        auto *browse = new QPushButton(d.type == SSHLX_C_FILESELECT ? tr("Browse...")
                                                                   : tr("Change..."));
        row->addWidget(browse);
        v->addLayout(row);
        connect(c.edit, &QLineEdit::textEdited, this, [this, id](const QString &t) {
            if (!m_updating)
                sshlx_dlg_set_text(m_dlg, id, t.toUtf8().constData());
        });
        const bool isFile = d.type == SSHLX_C_FILESELECT;
        const bool writing = d.for_writing;
        const QString ftitle = QString::fromUtf8(d.file_title ? d.file_title : "");
        connect(browse, &QPushButton::clicked, this, [this, id, isFile, writing, ftitle] {
            Ctrl &cc = m_ctrls[id];
            QString cur = cc.edit->text();
            QString result;
            if (isFile) {
                result = writing ? QFileDialog::getSaveFileName(this, ftitle, cur)
                                 : QFileDialog::getOpenFileName(this, ftitle, cur);
                if (result.isEmpty())
                    return;
            } else {
                bool ok = false;
                QFont f = QFontDialog::getFont(&ok, FontSpec::fromPutty(cur).font, this,
                                               tr("Select font"),
                                               QFontDialog::MonospacedFonts);
                if (!ok)
                    return;
                result = FontSpec::toPutty(f);
            }
            cc.edit->setText(result);
            sshlx_dlg_set_text(m_dlg, id, result.toUtf8().constData());
        });
        c.focusWidget = c.edit;
        if (c.label)
            c.label->setBuddy(c.edit);
        break;
    }
    case SSHLX_C_COLUMNS:
        return nullptr;
    }
    if (c.focusWidget)
        m_widgetIds[c.focusWidget] = id;
    m_ctrls.insert(id, c);
    return container;
}

void SettingsDialog::refreshCtrl(int id)
{
    auto it = m_ctrls.find(id);
    if (it == m_ctrls.end())
        return;
    Ctrl &c = it.value();
    m_updating = true;
    switch (c.type) {
    case SSHLX_C_TEXT:
        c.label->setText(QString::fromUtf8(sshlx_dlg_get_text(m_dlg, id)));
        break;
    case SSHLX_C_CHECKBOX:
        c.check->setChecked(sshlx_dlg_get_int(m_dlg, id));
        break;
    case SSHLX_C_RADIO:
        if (QAbstractButton *b = c.radios->button(sshlx_dlg_get_int(m_dlg, id)))
            b->setChecked(true);
        break;
    case SSHLX_C_EDITBOX:
    case SSHLX_C_FILESELECT:
    case SSHLX_C_FONTSELECT: {
        if (c.combo) {
            size_t n = sshlx_dlg_list_count(m_dlg, id);
            if (size_t(c.combo->count()) != n) {
                c.combo->clear();
                for (size_t i = 0; i < n; i++)
                    c.combo->addItem(QString::fromUtf8(sshlx_dlg_list_item(m_dlg, id, i)));
            }
        }
        QString t = QString::fromUtf8(sshlx_dlg_get_text(m_dlg, id));
        if (c.edit->text() != t)
            c.edit->setText(t);
        break;
    }
    case SSHLX_C_LISTBOX: {
        size_t n = sshlx_dlg_list_count(m_dlg, id);
        if (c.combo) {
            c.combo->clear();
            int sel = -1;
            for (size_t i = 0; i < n; i++) {
                c.combo->addItem(QString::fromUtf8(sshlx_dlg_list_item(m_dlg, id, i)));
                if (sshlx_dlg_list_selected(m_dlg, id, i))
                    sel = int(i);
            }
            c.combo->setCurrentIndex(sel);
        } else {
            QTreeWidgetItem *current = nullptr;
            c.list->clear();
            for (size_t i = 0; i < n; i++) {
                auto *item = new QTreeWidgetItem(c.list);
                QStringList parts = QString::fromUtf8(sshlx_dlg_list_item(m_dlg, id, i)).split('\t');
                for (int k = 0; k < parts.size() && k < c.list->columnCount(); k++)
                    item->setText(k, parts[k]);
                if (sshlx_dlg_list_selected(m_dlg, id, i)) {
                    item->setSelected(true);
                    current = item;
                }
            }
            if (current)
                c.list->setCurrentItem(current, 0, QItemSelectionModel::NoUpdate);
            for (int k = 0; k < c.list->columnCount(); k++)
                c.list->resizeColumnToContents(k);
        }
        break;
    }
    default:
        break;
    }
    m_updating = false;
}

void SettingsDialog::refreshAll()
{
    for (auto it = m_ctrls.begin(); it != m_ctrls.end(); ++it)
        refreshCtrl(it.key());
}

// ---- engine -> UI callbacks -------------------------------------------------
void SettingsDialog::cbChanged(void *ctx, int id)
{
    static_cast<SettingsDialog *>(ctx)->refreshCtrl(id);
}

void SettingsDialog::cbLabel(void *ctx, int id, const char *label)
{
    auto *d = static_cast<SettingsDialog *>(ctx);
    auto it = d->m_ctrls.find(id);
    if (it == d->m_ctrls.end())
        return;
    QString t = mnemonic(QString::fromUtf8(label), it->shortcut);
    if (it->label)
        it->label->setText(t);
    else if (it->check)
        it->check->setText(t);
    else if (it->button)
        it->button->setText(t);
}

void SettingsDialog::cbFocus(void *ctx, int id)
{
    auto *d = static_cast<SettingsDialog *>(ctx);
    auto it = d->m_ctrls.find(id);
    if (it != d->m_ctrls.end() && it->focusWidget)
        it->focusWidget->setFocus();
}

void SettingsDialog::cbSelectRange(void *ctx, int id, size_t start, size_t len)
{
    auto *d = static_cast<SettingsDialog *>(ctx);
    auto it = d->m_ctrls.find(id);
    if (it != d->m_ctrls.end() && it->edit)
        it->edit->setSelection(int(start), int(len));
}

void SettingsDialog::cbBeep(void *) { QApplication::beep(); }

void SettingsDialog::cbError(void *ctx, const char *msg)
{
    DialogKit::error(static_cast<SettingsDialog *>(ctx), tr("Invalid setting"),
                     QString::fromUtf8(msg));
}

void SettingsDialog::cbEnd(void *ctx, int value)
{
    auto *d = static_cast<SettingsDialog *>(ctx);
    // Close after the PuTTY handler that called dlg_end() has returned.
    QMetaObject::invokeMethod(d, [d, value] {
        d->done(value > 0 ? QDialog::Accepted : QDialog::Rejected);
    }, Qt::QueuedConnection);
}

void SettingsDialog::cbColour(void *ctx, int id, int r, int g, int b)
{
    auto *d = static_cast<SettingsDialog *>(ctx);
    QColor c = QColorDialog::getColor(QColor(r, g, b), d, tr("Select colour"));
    sshlx_dlg_colour_result(d->m_dlg, id, c.isValid(), c.red(), c.green(), c.blue());
}

void SettingsDialog::cbCaConfig(void *ctx)
{
    auto *d = static_cast<SettingsDialog *>(ctx);
    if (!d->m_caDialog) {
        d->m_caDialog = createCaConfig(d);
        d->m_caDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    d->m_caDialog->show();
    d->m_caDialog->raise();
}

void SettingsDialog::cbAbout(void *ctx)
{
    emit static_cast<SettingsDialog *>(ctx)->aboutRequested();
}
