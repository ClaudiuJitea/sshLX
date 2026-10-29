# Patch for Qt Advanced Docking System 4.4.1 (LGPL), applied by FetchContent.
#
# ADS uses Qt's *private* QPlatformNativeInterface (qpa headers, i.e. the
# Qt6::GuiPrivate target) only to obtain the X11 xcb connection. Qt >= 6.2
# provides the same through public API, QNativeInterface::QX11Application.
# Using it lets sshLX build where Qt's private development headers are not
# installed. Behaviour is unchanged.
#
# Second fix: CDockAreaTabBar defers ensureWidgetVisible(TabWidget) with a
# zero timer holding a raw pointer; if that tab is deleted first (several
# tabs closed in quick succession) it dereferences freed memory. Guard it
# with a QPointer.
#
# Third fix: CDockManager connects QApplication::focusWindowChanged to a
# lambda capturing `this` with no context object, so after a dock manager is
# destroyed the next focus change calls raise() on freed memory. Pass `this`
# as the context so Qt drops the connection with the object.
#
# Idempotent: safe to re-run.

function(patch_file file)
  file(READ "${file}" content)
  set(orig "${content}")
  math(EXPR last "${ARGC} - 1")
  foreach(i RANGE 1 ${last} 2)
    math(EXPR j "${i} + 1")
    # Skip replacements already applied (the new text may contain the old).
    string(FIND "${content}" "${ARGV${j}}" applied)
    if(applied EQUAL -1)
      string(REPLACE "${ARGV${i}}" "${ARGV${j}}" content "${content}")
    endif()
  endforeach()
  if(NOT content STREQUAL orig)
    file(WRITE "${file}" "${content}")
    message(STATUS "sshLX: patched ${file}")
  endif()
endfunction()

patch_file("src/ads_globals.cpp"
  "#include <qpa/qplatformnativeinterface.h>"
  "#include <QGuiApplication>\n#include <QtGui/qguiapplication_platform.h> /* sshLX patch: public X11 API */"
  "	QPlatformNativeInterface *native = qApp->platformNativeInterface();
	if (!native)
		return nullptr;

	void *connection = native->nativeResourceForIntegration(QByteArray(\"connection\"));
	return reinterpret_cast<xcb_connection_t *>(connection);"
  "	/* sshLX patch: public API instead of QPlatformNativeInterface. */
#if QT_CONFIG(xcb)
	auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
	return x11 ? x11->connection() : nullptr;
#else
	return nullptr;
#endif")

patch_file("src/CMakeLists.txt"
  "    set(ads_DEP_LIBS Core Gui GuiPrivate Widgets)"
  "    set(ads_DEP_LIBS Core Gui Widgets) # sshLX patch: no GuiPrivate"
  "    include_directories(\${Qt\${QT_VERSION_MAJOR}Gui_PRIVATE_INCLUDE_DIRS})"
  "    # sshLX patch: no Qt private include dirs"
  "    target_link_libraries(\${library_name} PRIVATE Qt6::GuiPrivate) #needed for <qpa/qplatformnativeinterface.h>"
  "    # sshLX patch: Qt6::GuiPrivate no longer needed")

patch_file("src/DockAreaTabBar.cpp"
  "#include <QtGlobal>"
  "#include <QtGlobal>\n#include <QPointer> /* sshLX patch */"
  "			QTimer::singleShot(0, _this, [&, TabWidget]
			{
				_this->ensureWidgetVisible(TabWidget);
			});"
  "			/* sshLX patch: the tab may be deleted before the timer fires. */
			QPointer<CDockWidgetTab> Tab(TabWidget);
			auto Bar = _this;
			QTimer::singleShot(0, _this, [Bar, Tab]
			{
				if (Tab)
					Bar->ensureWidgetVisible(Tab);
			});")

patch_file("src/DockManager.cpp"
  "connect(qApp, &QApplication::focusWindowChanged, [this](QWindow* focusWindow)"
  "connect(qApp, &QApplication::focusWindowChanged, this, [this](QWindow* focusWindow) /* sshLX patch */")

# Fourth fix: on Linux the dock manager emulates "floating windows stay above
# the main window" by toggling Qt::WindowStaysOnTopHint on every main-window
# (de)activation when the platform is not xcb. Changing window flags on a
# visible window hides and recreates it, so on Wayland the first click on the
# main window made a detached tab vanish (and its hide handler closed the
# tab), leaving a corrupted window and an app that looked frozen. Wayland has
# no stays-on-top anyway; the floating windows are transient children of the
# main window, so the compositor already keeps them above it. Only do the
# emulation on xcb, through the X properties it already uses there.
patch_file("src/DockManager.cpp"
  "			else
			{
                    _window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
			}"
  "			/* sshLX patch: no window-flag toggling off xcb (it hides the window). */"
  "            else
			{
				_window->setWindowFlag(Qt::WindowStaysOnTopHint, false);
			}
			_window->raise();"
  "            else
			{
				/* sshLX patch: no window-flag toggling off xcb (it hides the window). */
				continue;
			}
			_window->raise();")

# Fifth fix: CFloatingDockContainer::show() calls winId() for X11-only
# taskbar hints on every platform. winId() makes the floating window native
# and, since it is a child of the dock manager, Qt then turns the dock
# manager, its splitters and the menu bar into native child windows too. On
# Wayland those are decorated subsurfaces: the main window showed a second
# title bar with its own buttons and stopped responding normally. Only touch
# the native handle on xcb.
patch_file("src/FloatingDockContainer.cpp"
  "	internal::xcb_add_prop(true, winId(), \"_NET_WM_STATE\", \"_NET_WM_STATE_SKIP_TASKBAR\");
	internal::xcb_add_prop(true, winId(), \"_NET_WM_STATE\", \"_NET_WM_STATE_SKIP_PAGER\");"
  "	/* sshLX patch: winId() only on xcb; elsewhere it makes the main window's
	   widgets native child windows (decorated subsurfaces on Wayland). */
	if (QGuiApplication::platformName() == QLatin1String(\"xcb\"))
	{
		internal::xcb_add_prop(true, winId(), \"_NET_WM_STATE\", \"_NET_WM_STATE_SKIP_TASKBAR\");
		internal::xcb_add_prop(true, winId(), \"_NET_WM_STATE\", \"_NET_WM_STATE_SKIP_PAGER\");
	}")

# Sixth fix: "Detach Group" did nothing in an area holding a non-floatable
# tab (sshLX's Start page), because the whole group then cannot float. Let
# the button detach the current tab instead when that tab can float, and
# refresh the button whenever the current tab changes.
patch_file("src/DockAreaWidget.cpp"
  "		TitleBar->button(TitleBarButtonUndock)->setEnabled(
			_this->features().testFlag(CDockWidget::DockWidgetFloatable));"
  "		/* sshLX patch: or the current tab alone, if it can float. */
		auto Current = _this->currentDockWidget();
		TitleBar->button(TitleBarButtonUndock)->setEnabled(
			_this->features().testFlag(CDockWidget::DockWidgetFloatable)
			|| (Current && Current->features().testFlag(CDockWidget::DockWidgetFloatable)));"
  "	d->ContentsLayout->currentWidget()->show();
	Q_EMIT currentChanged(index);"
  "	d->ContentsLayout->currentWidget()->show();
	d->updateTitleBarButtonStates(); /* sshLX patch */
	Q_EMIT currentChanged(index);")

patch_file("src/DockAreaTitleBar.cpp"
  "		d->makeAreaFloating(mapFromGlobal(QCursor::pos()), DraggingInactive);
	}
}"
  "		d->makeAreaFloating(mapFromGlobal(QCursor::pos()), DraggingInactive);
	}
	/* sshLX patch: the group cannot float; detach the current tab. */
	else if (auto Current = d->DockArea->currentDockWidget();
	         Current && Current->features().testFlag(CDockWidget::DockWidgetFloatable))
	{
		Current->setFloating();
	}
}")

# Seventh fix: the floating window's own title bar drags the window by
# calling move() on every mouse move. Wayland clients cannot position their
# windows, so the window stayed put. Off xcb, hand the drag to the compositor
# with QWindow::startSystemMove() (re-docking by drag is not available there).
patch_file("src/linux/FloatingWidgetTitleBar.cpp"
  "#include <QMouseEvent>"
  "#include <QMouseEvent>\n#include <QGuiApplication>\n#include <QWindow> /* sshLX patch */"
  "	if (ev->button() == Qt::LeftButton)
	{
		d->DragState = DraggingFloatingWidget;"
  "	/* sshLX patch: only the compositor can move windows on Wayland. */
	if (ev->button() == Qt::LeftButton && QGuiApplication::platformName() != QLatin1String(\"xcb\")
	    && window()->windowHandle() && window()->windowHandle()->startSystemMove())
	{
		return;
	}
	if (ev->button() == Qt::LeftButton)
	{
		d->DragState = DraggingFloatingWidget;")

# Eighth fix: ADS calls activateWindow() again right after showing a floating
# window, after a drag and after a drop, although Qt already activates a
# window when it is shown. On Wayland every call requests an xdg-activation
# token; the redundant ones are never used, and GNOME shows the busy
# (spinning) pointer until each unused token times out. Keep the calls on
# xcb; elsewhere only activate a window that is not already active, and leave
# the show-time activation to Qt.
patch_file("src/FloatingDockContainer.cpp"
  "    if (CDockManager::testConfigFlag(CDockManager::FocusHighlighting))
    {
        this->window()->activateWindow();
    }"
  "    /* sshLX patch: Qt activates a shown window itself; xcb only. */
    if (CDockManager::testConfigFlag(CDockManager::FocusHighlighting)
        && QGuiApplication::platformName() == QLatin1String(\"xcb\"))
    {
        this->window()->activateWindow();
    }"
  "	setWindowOpacity(1);
	activateWindow();"
  "	setWindowOpacity(1);
	if (!isActiveWindow()) /* sshLX patch: no redundant activation */
		activateWindow();")

patch_file("src/DockContainerWidget.cpp"
  "	window()->activateWindow();
	if (SingleDroppedDockWidget)"
  "	if (!window()->isActiveWindow()) /* sshLX patch: no redundant activation */
		window()->activateWindow();
	if (SingleDroppedDockWidget)"
  "	window()->activateWindow();
	d->DockManager->notifyWidgetOrAreaRelocation(Widget);"
  "	if (!window()->isActiveWindow()) /* sshLX patch: no redundant activation */
		window()->activateWindow();
	d->DockManager->notifyWidgetOrAreaRelocation(Widget);")

patch_file("src/DockWidget.cpp"
  "		FloatingWindow->raise();
		FloatingWindow->activateWindow();"
  "		FloatingWindow->raise();
		if (!FloatingWindow->isActiveWindow()) /* sshLX patch: no redundant activation */
			FloatingWindow->activateWindow();")

# Ninth fix: the floating title bar creates its label and buttons without a
# parent and calls setVisible(true) on them before they are laid out, which
# briefly makes each one a top-level window. On Wayland each of these windows
# requests an xdg-activation token that is never used; GNOME shows the busy
# (spinning) pointer until they time out. Create them inside the title bar.
patch_file("src/linux/FloatingWidgetTitleBar.cpp"
  "	TitleLabel = new tTabLabel();"
  "	TitleLabel = new tTabLabel(_this); /* sshLX patch: never a top-level window */"
  "	CloseButton = new tCloseButton();"
  "	CloseButton = new tCloseButton(_this); /* sshLX patch */"
  "	MaximizeButton = new tMaximizeButton();"
  "	MaximizeButton = new tMaximizeButton(_this); /* sshLX patch */")
