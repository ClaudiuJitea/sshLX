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
