#include "optionsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "icons.h"
#include "keyring.h"
#include "mainwindow.h"
#include "serialmanager.h"
#include "shortcuts.h"
#include "theme.h"

using namespace DialogKit;

// UI font: the desktop's, but never below 11 pt unless the user chose a
// size in Options (ui/fontSize; 0 = automatic).
void applyUiFontSize()
{
    static const double desktopSize = QApplication::font().pointSizeF();
    QFont f = QApplication::font();
    const double chosen = QSettings().value("ui/fontSize", 0.0).toDouble();
    f.setPointSizeF(chosen > 0 ? chosen : std::max(desktopSize, 11.0));
    QApplication::setFont(f);
}

static QFormLayout *form()
{
    auto *f = new QFormLayout;
    f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    f->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    f->setHorizontalSpacing(16);
    f->setVerticalSpacing(10);
    f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    return f;
}

static QWidget *page(const QString &title, const QString &subtitle,
                     const QList<QWidget *> &content, bool stretchLast = false)
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(24, 20, 24, 8);
    v->setSpacing(14);
    v->addWidget(header(title, subtitle));
    for (int i = 0; i < content.size(); i++)
        v->addWidget(content[i], stretchLast && i == content.size() - 1 ? 1 : 0);
    if (!stretchLast)
        v->addStretch(1);
    return w;
}

OptionsDialog::OptionsDialog(MainWindow *main) : QDialog(main), m_main(main)
{
    setWindowTitle(tr("Options"));
    resize(860, 640);
    QSettings st;

    auto *root = new QHBoxLayout;
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- navigation ----
    auto *navPane = new QWidget;
    navPane->setObjectName("NavPane");
    navPane->setFixedWidth(200);
    auto *nv = new QVBoxLayout(navPane);
    nv->setContentsMargins(10, 16, 10, 10);
    auto *nav = new QListWidget;
    nav->setObjectName("Nav");
    nav->setIconSize(QSize(18, 18));
    nav->setFocusPolicy(Qt::NoFocus);
    nv->addWidget(nav);
    root->addWidget(navPane);
    auto *stack = new QStackedWidget;
    root->addWidget(stack, 1);
    auto addPage = [&](const QString &icon, const QString &name, QWidget *p) {
        auto *it = new QListWidgetItem(Icons::get(icon), name, nav);
        it->setSizeHint(QSize(0, 38));   // QSS item padding alone is not honoured
        // Pages scroll instead of squeezing their cards when the dialog is small.
        auto *sa = new QScrollArea;
        sa->setObjectName("PageScroll");
        sa->setFrameShape(QFrame::NoFrame);
        sa->setWidgetResizable(true);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setWidget(p);
        stack->addWidget(sa);
    };

    // ---- General ----
    m_theme = new QComboBox;
    m_theme->addItem(tr("System"), Theme::System);
    m_theme->addItem(tr("Dark"), Theme::Dark);
    m_theme->addItem(tr("Light"), Theme::Light);
    m_theme->setCurrentIndex(m_theme->findData(Theme::mode()));
    m_theme->setMinimumWidth(220);
    m_fontSize = new QDoubleSpinBox;
    m_fontSize->setRange(0, 24);
    m_fontSize->setSingleStep(0.5);
    m_fontSize->setDecimals(1);
    m_fontSize->setSpecialValueText(tr("Automatic"));
    m_fontSize->setSuffix(tr(" pt"));
    m_fontSize->setValue(st.value("ui/fontSize", 0.0).toDouble());
    m_fontSize->setMinimumWidth(220);
    auto *appearance = form();
    appearance->addRow(tr("Theme"), m_theme);
    appearance->addRow(tr("Interface font size"), m_fontSize);
    auto *appearanceBox = new QVBoxLayout;
    appearanceBox->addLayout(appearance);
    appearanceBox->addWidget(hint(tr("System follows your desktop's dark or light setting. "
                                     "Automatic uses the desktop font, at least 11 pt.")));

    m_restore = new QCheckBox(tr("Restore open sessions on startup"));
    m_restore->setChecked(st.value("behaviour/restoreSessions", false).toBool());
    m_reuseTab = new QCheckBox(tr("Switch to an open tab instead of opening another"));
    m_reuseTab->setChecked(st.value("behaviour/reuseTab", true).toBool());
    m_closeOnExit = new QCheckBox(tr("Close tabs when the session ends"));
    m_closeOnExit->setChecked(st.value("behaviour/closeTabOnExit", false).toBool());
    auto *sessions = new QVBoxLayout;
    sessions->setSpacing(10);
    sessions->addWidget(withHint(m_restore, tr("Reopens the tabs and their dock layout "
                                               "from the last session.")));
    sessions->addWidget(withHint(m_reuseTab, tr("Opening a connection that already has a tab "
                                                "brings that tab forward. \"Open in New Tab\" "
                                                "in the tree's menu always opens another.")));
    sessions->addWidget(withHint(m_closeOnExit, tr("As PuTTY's \"Close window on exit\". "
                                                   "Otherwise the tab stays open with a "
                                                   "Reconnect bar.")));
    addPage("settings", tr("General"),
            page(tr("General"), tr("Appearance and session behaviour"),
                 {card(tr("Appearance"), appearanceBox), card(tr("Sessions"), sessions)}));

    // ---- Serial ----
    addPage("usb", tr("Serial"), buildSerial());
    // ---- Shortcuts ----
    addPage("keyboard", tr("Shortcuts"), buildShortcuts());

    // ---- Passwords ----
    m_keyring = new QCheckBox(tr("Store connection passwords in the system keyring"));
    m_keyring->setChecked(Keyring::enabled());
    m_keyring->setEnabled(Keyring::available());
    auto *pw = new QVBoxLayout;
    pw->setSpacing(10);
    pw->addWidget(withHint(m_keyring,
        Keyring::available()
            ? tr("Passwords are kept only in the keyring (Secret Service / KWallet), never "
                 "in a file, and offered once when the server asks. If one is rejected, "
                 "PuTTY's normal prompt appears in the terminal.")
            : tr("Not available: sshLX was built without QtKeychain.")));
    addPage("lock", tr("Passwords"),
            page(tr("Passwords"), tr("PuTTY itself never stores passwords; this is opt-in"),
                 {card(QString(), pw)}));

    nav->setCurrentRow(0);
    connect(nav, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);

    // ---- footer ----
    QPushButton *cancel = secondaryButton(tr("Cancel"));
    QPushButton *save = primaryButton(tr("Save"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] { apply(); accept(); });
    auto *footerWrap = new QWidget;
    auto *fw = new QVBoxLayout(footerWrap);
    fw->setContentsMargins(24, 0, 24, 16);
    fw->addWidget(footer({cancel, save}));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addLayout(root, 1);
    outer->addWidget(footerWrap);
}

QWidget *OptionsDialog::buildSerial()
{
    QSettings st;
    m_udev = new QCheckBox(tr("Use udev for instant detection"));
    m_udev->setChecked(st.value("serial/udev", true).toBool());
    m_desktop = new QCheckBox(tr("Show a desktop notification"));
    m_desktop->setChecked(st.value("notifications/desktop", false).toBool());
    auto *det = new QVBoxLayout;
    det->setSpacing(10);
    det->addWidget(withHint(m_udev, tr("Otherwise sshLX checks every 0.5 s. Takes effect after "
                                       "a restart. In use now: %1.")
                                        .arg(m_main->serialManager()->ports()->backend())));
    det->addWidget(withHint(m_desktop, tr("In addition to the banner inside sshLX, when a "
                                          "device is plugged in.")));

    m_rules = new QTableWidget(0, 4);
    m_rules->setHorizontalHeaderLabels({tr("Device"), tr("When plugged in"), tr("Line"),
                                        tr("Identity")});
    m_rules->horizontalHeader()->setStretchLastSection(true);
    m_rules->horizontalHeader()->setHighlightSections(false);
    m_rules->verticalHeader()->hide();
    m_rules->setShowGrid(false);
    m_rules->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rules->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rules->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rules->setMinimumHeight(140);
    static const char *parity = "NOEMS";
    for (const SerialRule &r : m_main->serialManager()->rules().rules()) {
        int row = m_rules->rowCount();
        m_rules->insertRow(row);
        m_rules->setItem(row, 0, new QTableWidgetItem(r.label));
        m_rules->setItem(row, 1, new QTableWidgetItem(
            r.hidden ? tr("Hidden from the list")
            : r.action == SerialRule::AutoConnect ? tr("Connect automatically")
            : r.action == SerialRule::Ignore ? tr("Ignore") : tr("Ask")));
        m_rules->setItem(row, 2, new QTableWidgetItem(
            QStringLiteral("%1 %2%3%4").arg(r.speed > 0 ? QString::number(r.speed) : tr("auto"))
                .arg(r.dataBits)
                .arg(QChar(parity[qBound(0, r.parity, 4)]))
                .arg(r.stopHalfBits == 2 ? "1" : r.stopHalfBits == 3 ? "1.5" : "2")));
        m_rules->setItem(row, 3, new QTableWidgetItem(r.key));
    }
    m_rules->resizeColumnsToContents();
    auto *del = secondaryButton(tr("Remove rule"));
    del->setEnabled(false);
    connect(m_rules, &QTableWidget::itemSelectionChanged, del,
            [this, del] { del->setEnabled(m_rules->currentRow() >= 0); });
    connect(del, &QPushButton::clicked, this, [this] {
        int row = m_rules->currentRow();
        if (row < 0)
            return;
        m_main->serialManager()->rules().remove(m_rules->item(row, 3)->text());
        m_main->serialManager()->saveRules();
        m_main->serialManager()->ports()->refilter();   // a hidden port shows again
        m_rules->removeRow(row);
    });
    auto *rules = new QVBoxLayout;
    rules->setSpacing(10);
    rules->addWidget(hint(m_rules->rowCount()
                              ? tr("What happens when each known device is plugged in. "
                                   "Removing a rule also shows a hidden port again.")
                              : tr("No rules yet. Choose \"Always connect\" or \"Never ask\" "
                                   "when a device is detected, or \"Hide Port\" in the "
                                   "Serial Ports list, to create one.")));
    rules->addWidget(m_rules, 1);
    auto *row = new QHBoxLayout;
    row->addWidget(del);
    row->addStretch(1);
    rules->addLayout(row);

    return page(tr("Serial"), tr("USB and serial device detection"),
                {card(tr("Detection"), det), card(tr("Device rules"), rules)}, true);
}

QWidget *OptionsDialog::buildShortcuts()
{
    const QList<Shortcuts::Entry> entries = Shortcuts::instance().entries();
    m_shortcuts = new QTableWidget(entries.size(), 2);
    m_shortcuts->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut")});
    m_shortcuts->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcuts->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_shortcuts->horizontalHeader()->resizeSection(1, 220);
    m_shortcuts->horizontalHeader()->setHighlightSections(false);
    m_shortcuts->verticalHeader()->hide();
    m_shortcuts->verticalHeader()->setDefaultSectionSize(40);
    m_shortcuts->setShowGrid(false);
    m_shortcuts->setSelectionMode(QAbstractItemView::NoSelection);
    m_shortcuts->setFocusPolicy(Qt::NoFocus);
    for (int i = 0; i < entries.size(); i++) {
        auto *name = new QTableWidgetItem(entries[i].name);
        name->setFlags(Qt::ItemIsEnabled);
        name->setData(Qt::UserRole, entries[i].id);
        m_shortcuts->setItem(i, 0, name);
        auto *ed = new QKeySequenceEdit(entries[i].seq);
        ed->setClearButtonEnabled(true);
        ed->setMaximumSequenceLength(1);
        auto *cell = new QWidget;
        auto *cl = new QHBoxLayout(cell);
        cl->setContentsMargins(4, 3, 8, 3);
        cl->addWidget(ed);
        m_shortcuts->setCellWidget(i, 1, cell);
    }
    auto *reset = secondaryButton(tr("Restore defaults"));
    connect(reset, &QPushButton::clicked, this, [this] {
        const QList<Shortcuts::Entry> e = Shortcuts::instance().entries();
        for (int i = 0; i < e.size(); i++)
            m_shortcuts->cellWidget(i, 1)->findChild<QKeySequenceEdit *>()
                ->setKeySequence(e[i].defaultSeq);
    });
    auto *box = new QVBoxLayout;
    box->setSpacing(10);
    box->addWidget(m_shortcuts, 1);
    auto *row = new QHBoxLayout;
    row->addWidget(reset);
    row->addStretch(1);
    box->addLayout(row);
    return page(tr("Shortcuts"),
                tr("Only these keys are taken by sshLX; every other key goes to the "
                   "terminal. Clear one to give its keys to the terminal, or press "
                   "\"Send Next Shortcut to Terminal\" first."),
                {card(QString(), box)}, true);
}

void OptionsDialog::apply()
{
    QSettings st;
    st.setValue("behaviour/restoreSessions", m_restore->isChecked());
    st.setValue("behaviour/closeTabOnExit", m_closeOnExit->isChecked());
    st.setValue("behaviour/reuseTab", m_reuseTab->isChecked());
    st.setValue("serial/udev", m_udev->isChecked());
    st.setValue("notifications/desktop", m_desktop->isChecked());
    Keyring::setEnabled(m_keyring->isChecked());
    st.setValue("ui/fontSize", m_fontSize->value());
    applyUiFontSize();
    Theme::setMode(Theme::Mode(m_theme->currentData().toInt()));
    m_main->applyTheme();

    // Shortcuts: refuse duplicates rather than silently shadowing.
    QHash<QString, QString> seen;
    for (int i = 0; i < m_shortcuts->rowCount(); i++) {
        QKeySequence seq =
            m_shortcuts->cellWidget(i, 1)->findChild<QKeySequenceEdit *>()->keySequence();
        QString k = seq.toString();
        if (!k.isEmpty() && seen.contains(k)) {
            warning(this, tr("Shortcut conflict"),
                    tr("%1 is assigned to both \"%2\" and \"%3\"; the second assignment "
                       "was not applied.").arg(k, seen[k], m_shortcuts->item(i, 0)->text()));
            continue;
        }
        seen.insert(k, m_shortcuts->item(i, 0)->text());
        Shortcuts::instance().setSequence(m_shortcuts->item(i, 0)->data(Qt::UserRole).toString(), seq);
    }
    Shortcuts::instance().save();
}
