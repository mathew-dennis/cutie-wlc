#include <cutie-wlc.h>
#include <glwindow.h>
#include <output-management-v1.h>
#include <output-power-management-v1.h>
#include <screencopy.h>
#include <foreign-toplevel-management.h>
#include <input-method-v2.h>

#include <QtWaylandCompositor/QWaylandSeat>
#include <QtWaylandCompositor/QWaylandOutput>
#include <QGuiApplication>
#include <QScreen>
#include <QWaylandPointer>
#include <QWaylandTouch>
#include <QTouchEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>

CwlCompositor::CwlCompositor(GlWindow *glwindow)
	: m_glwindow(glwindow)
	, m_xdgShell(new QWaylandXdgShell(this))
	, m_layerShell(new LayerShellV1(this))
	, m_xdgdecoration(new QWaylandXdgDecorationManagerV1())
{
	m_glwindow->setCompositor(this);
	connect(m_glwindow, &GlWindow::glReady, this, &CwlCompositor::create);
	connect(qGuiApp, &QGuiApplication::screenAdded, this,
		&CwlCompositor::onScreenAdded);
	connect(qGuiApp, &QGuiApplication::screenRemoved, this,
		&CwlCompositor::onScreenRemoved);

	connect(m_xdgShell, &QWaylandXdgShell::toplevelCreated, this,
		&CwlCompositor::onXdgToplevelCreated);
	connect(m_xdgShell, &QWaylandXdgShell::popupCreated, this,
		&CwlCompositor::onXdgPopupCreated);
	connect(m_layerShell, &LayerShellV1::layerShellSurfaceCreated, this,
		&CwlCompositor::onLayerShellSurfaceCreated);

	// Hide the pointer after a while without mouse activity.
	m_mouseInactivityTimer = new QTimer(this);
	m_mouseInactivityTimer->setInterval(3000);
	m_mouseInactivityTimer->setSingleShot(true);
	connect(m_mouseInactivityTimer, &QTimer::timeout, this, [this]() {
		m_mouseActive = false;
		triggerRender();
	});

	blurAnim->setDuration(250);
	unblurAnim->setDuration(250);
	launcherOpenAnim->setDuration(250);
	launcherCloseAnim->setDuration(250);
	blurAnim->setEndValue(1.0);
	unblurAnim->setEndValue(0.0);
	launcherOpenAnim->setEndValue(1.0);
	launcherCloseAnim->setEndValue(0.0);

	connect(blurAnim, &QVariantAnimation::valueChanged, this,
		&CwlCompositor::animationValueChanged);
	connect(unblurAnim, &QVariantAnimation::valueChanged, this,
		&CwlCompositor::animationValueChanged);
	connect(unblurAnim, &QVariantAnimation::finished, this,
		[this]() { m_workspace->showDesktop(true); });
	connect(launcherOpenAnim, &QVariantAnimation::valueChanged, this,
		&CwlCompositor::animationValueChanged);
	connect(launcherCloseAnim, &QVariantAnimation::valueChanged, this,
		&CwlCompositor::animationValueChanged);
}

CwlCompositor::~CwlCompositor()
{
	for (GlWindow *window : m_externalWindows)
		delete window;
}

void CwlCompositor::create()
{
	m_output = new QWaylandOutput(this, m_glwindow);
	const qreal refreshRate = m_glwindow->screen() ?
					  m_glwindow->screen()->refreshRate() :
					  0.0;
	QWaylandOutputMode mode(m_glwindow->size(),
				refreshRate > 0.0 ? qRound(refreshRate * 1000.0) :
						    60000);
	m_output->addMode(mode, true);
	QWaylandCompositor::create();
	m_output->setCurrentMode(mode);
	m_output->setScaleFactor(m_scaleFactor);
	m_glwindow->setOutput(m_output);

	// The default seat only exists after create().
	connect(defaultSeat(), &QWaylandSeat::cursorSurfaceRequest, this,
		[this](QWaylandSurface *surface, int hotspotX, int hotspotY) {
			delete m_cursorView;
			m_cursorView = nullptr;
			m_cursorHidden = !surface;
			m_cursorHotspot = QPoint(hotspotX, hotspotY);
			if (surface) {
				CwlView *view = new CwlView(this, QRect());
				m_cursorView = view;
				view->setSurface(surface);
				connect(surface, &QWaylandSurface::redraw, view,
					[this]() { triggerRender(); });
				connect(view, &QWaylandView::surfaceDestroyed, view,
					[this, view]() {
						if (m_cursorView == view)
							m_cursorView = nullptr;
						view->deleteLater();
						triggerRender();
					});
			}
			triggerRender();
		});

	m_xdgdecoration->setExtensionContainer(this);
	m_xdgdecoration->initialize();
	m_xdgdecoration->setPreferredMode(
		QWaylandXdgToplevel::ServerSideDecoration);

	m_workspace = new CwlWorkspace(this);
	m_cutieshell = new CutieShell(this);
	m_outputManager = new OutputManagerV1(this);
	m_outputPowerManager = new OutputPowerManagerV1(this);
	m_screencopyManager = new ScreencopyManagerV1(this);

	m_created = true;
	for (QScreen *screen : qGuiApp->screens())
		if (screen != m_glwindow->screen())
			onScreenAdded(screen);

	m_foreignTlManagerV1 = new ForeignToplevelManagerV1(this);
	connect(m_workspace, &CwlWorkspace::toplevelCreated,
		m_foreignTlManagerV1,
		&ForeignToplevelManagerV1::onToplevelCreated);
	connect(m_workspace, &CwlWorkspace::toplevelDestroyed,
		m_foreignTlManagerV1,
		&ForeignToplevelManagerV1::onToplevelDestroyed);

	initInputMethod();

	qputenv("CUTIE_SHELL", QByteArray("true"));
	qputenv("QT_QPA_PLATFORM", QByteArray("wayland"));
	qputenv("EGL_PLATFORM", QByteArray("wayland"));
	qputenv("QT_IM_MODULE", QByteArray("textinputv3"));
	qunsetenv("QT_QPA_GENERIC_PLUGINS");
	qunsetenv("QT_SCALE_FACTOR");
	qputenv("WAYLAND_DISPLAY", socketName());

	/*
		Setting QSG_NO_VSYNC and QSG_RENDER_LOOP makes resizing QtQuick apps
		much smoother. There is a QTBUG-51112 which MIGHT be related to our issue.
		But the bug describes actually a slightly different issue.

		Might also be the hwcomposer issue https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html
	*/
	qputenv("QSG_NO_VSYNC", QByteArray("1"));
	qputenv("QSG_RENDER_LOOP", QByteArray("basic"));

	QStringList args = QStringList();
	args.append("-c");
	args.append("cutie-home");
	if (!QProcess::startDetached("bash", args))
		qDebug() << "Failed to run";

	args = QStringList();
	args.append("-c");
	args.append(launcher);
	if (!QProcess::startDetached("bash", args))
		qDebug() << "Failed to run";

	args = QStringList();
	args.append("-c");
	args.append("cutie-panel");
	if (!QProcess::startDetached("bash", args))
		qDebug() << "Failed to run";

	args = QStringList();
	args.append("-c");
	args.append("cutie-keyboard");
	if (!QProcess::startDetached("bash", args))
		qDebug() << "Failed to run";

	args = QStringList();
	args.append("-c");
	args.append("env XDG_CURRENT_DESKTOP=GNOME /usr/libexec/feedbackd");
	if (!QProcess::startDetached("bash", args))
		qDebug() << "Failed to run";

	args = QStringList();
	args.append("-c");
	args.append("loginctl activate");
	if (!QProcess::startDetached("bash", args))
		qDebug() << "Failed to run";
}

QList<CwlView *> CwlCompositor::getViews() const
{
	return m_workspace->getViews();
}

QList<CwlView *> CwlCompositor::getToplevelViews()
{
	return m_workspace->getToplevelViews();
}

QWaylandOutput *CwlCompositor::outputForToplevel(CwlView *view)
{
	if (!view || view->getAppId() == "cutie-launcher")
		return m_output;
	if (view->getTopLevel() && view->getTopLevel()->parentToplevel()) {
		CwlView *parent = findTlView(view->getTopLevel()
						 ->parentToplevel()
						 ->xdgSurface()
						 ->surface());
		if (parent && parent->output())
			return parent->output();
	}
	return m_externalOutputs.isEmpty() ? m_output :
					      m_externalOutputs.constBegin().value();
}

QRect CwlCompositor::outputGeometry(QWaylandOutput *output) const
{
	if (!output || output == m_output)
		return m_workspace ? m_workspace->availableGeometry() : QRect();
	return m_workspace ? m_workspace->availableGeometry(output) :
			     outputFullGeometry(output);
}

QRect CwlCompositor::outputFullGeometry(QWaylandOutput *output) const
{
	if (!output || output == m_output)
		return m_workspace ? m_workspace->outputGeometry() : QRect();
	return QRect(output->position(),
		     output->currentMode().size() / qMax(1, output->scaleFactor()));
}

QList<QWaylandOutput *> CwlCompositor::externalOutputs() const
{
	return m_externalOutputs.values();
}

void CwlCompositor::onScreenAdded(QScreen *screen)
{
	if (!m_created || !screen || screen == m_glwindow->screen() ||
	    m_externalWindows.contains(screen))
		return;

	GlWindow *window = new GlWindow();
	window->setScreen(screen);
	// Global (virtual desktop) geometry, so the window sits on its screen and
	// Qt maps pointer positions to it correctly.
	window->setGeometry(QRect(screen->geometry().topLeft(), screen->size()));
	window->setCompositor(this);
	m_externalWindows.insert(screen, window);
	connect(window, &GlWindow::glReady, this,
		[this, screen, window]() { createExternalOutput(screen, window); });
	window->show();
}

void CwlCompositor::createExternalOutput(QScreen *screen, GlWindow *window)
{
	if (!m_created || !screen || !window || m_externalOutputs.contains(screen))
		return;

	const qreal refreshRate = screen->refreshRate() > 0
				      ? screen->refreshRate() : 60.0;
	QWaylandOutput *output = new QWaylandOutput(this, window);
	const QWaylandOutputMode mode(window->size(), qRound(refreshRate * 1000));
	output->addMode(mode, true);
	output->setCurrentMode(mode);
	output->setScaleFactor(1);
	// Place the output to the right of everything that already exists, in
	// the shared logical space, independent of what the QPA reports.
	int x = outputFullGeometry(m_output).right() + 1;
	for (QWaylandOutput *other : m_externalOutputs)
		x = qMax(x, outputFullGeometry(other).right() + 1);
	output->setPosition(QPoint(x, 0));
	output->setPhysicalSize(screen->physicalSize().toSize());
	output->setModel(screen->name());
	m_externalOutputs.insert(screen, output);
	window->setOutput(output);
	qInfo() << "Cutie: screens: primary" << m_glwindow->screen()->geometry()
		<< "virtual" << m_glwindow->screen()->virtualGeometry()
		<< "external" << screen->name() << screen->geometry()
		<< "window" << window->geometry() << "output position"
		<< output->position();
	routeExistingViews(m_output, output);
	triggerRender();
}

void CwlCompositor::onScreenRemoved(QScreen *screen)
{
	if (!screen)
		return;
	QWaylandOutput *output = m_externalOutputs.take(screen);
	GlWindow *window = m_externalWindows.take(screen);
	if (!output && !window)
		return;
	if (output && m_cursorOutput == output)
		m_cursorOutput = nullptr;

	if (output) {
		routeExistingViews(output, m_output);
		for (CwlView *view : m_workspace->getAllViews()) {
			LayerSurfaceV1 *layerSurface = view->getLayerSurface();
			if (view->output() != output || !layerSurface)
				continue;
			layerSurface->output = m_output;
			view->setDesktopOutput(m_output, outputGeometry(m_output));
			view->onLayerSurfaceDataChanged(layerSurface);
		}
	}
	if (window)
		window->setOutput(nullptr);
	delete output;
	delete window;
	triggerRender();
}

void CwlCompositor::routeExistingViews(QWaylandOutput *from,
				       QWaylandOutput *to)
{
	if (!from || !to || from == to || !m_workspace)
		return;

	const QRect targetGeometry = outputGeometry(to);
	for (CwlView *root : m_workspace->getToplevelViews()) {
		if (root->output() != from || root->getAppId() == "cutie-launcher")
			continue;

		const QPointF positionDelta = targetGeometry.topLeft() - root->getPosition();
		QList<CwlView *> pending{root};
		while (!pending.isEmpty()) {
			CwlView *view = pending.takeLast();
			pending.append(view->getChildViews());
			if (view == root) {
				view->setDesktopOutput(to, targetGeometry);
				continue;
			}
			view->setOutput(to);
			view->setPosition(view->getPosition() + positionDelta);
			if (view->isToplevel() && view->getTopLevel())
				view->getTopLevel()->sendMaximized(targetGeometry.size());
		}
	}
}

CwlView *CwlCompositor::viewAt(const QPoint &position, QWaylandOutput *output)
{
	if (!output)
		output = m_output;
	if (!output)
		return nullptr;
	const qreal outputScale = qMax(1, output->scaleFactor());
	const QPointF checkPoint = output->position() + QPointF(position) / outputScale;
	for (auto it = getViews().crbegin(); it != getViews().crend(); ++it) {
		CwlView *view = *it;
		if (view->output() != output)
			continue;
		QRectF geom(view->getPosition(), view->size());
		if (!geom.contains(checkPoint))
			continue;
		for (CwlView *childView : view->getChildViews()) {
			if (childView->output() != output)
				continue;
			QRectF childGeometry(childView->getPosition(), childView->size());
			if (childGeometry.contains(checkPoint))
				return childView;
		}
		return view;
	}
	return nullptr;
}

CwlView *CwlCompositor::findView(QWaylandSurface *s)
{
	for (CwlView *view : getViews()) {
		if (view->surface() == s)
			return view;
	}
	return nullptr;
}

CwlView *CwlCompositor::findTlView(QWaylandSurface *s)
{
	CwlView *ret = nullptr;
	for (CwlView *view : m_workspace->getToplevelViews()) {
		if (view->surface() == s)
			ret = view;
		else if (view->getChildViews().size() > 0)
			ret = findTreeView(s, view);
	}
	return ret;
}

CwlView *CwlCompositor::findTreeView(QWaylandSurface *s, CwlView *rootView)
{
	CwlView *ret = nullptr;
	for (CwlView *childView : rootView->getChildViews()) {
		if (childView->surface() == s)
			ret = childView;
		else if (childView->getChildViews().size() > 0)
			ret = findTreeView(s, childView);
	}
	return ret;
}

void CwlCompositor::onXdgToplevelCreated(QWaylandXdgToplevel *toplevel,
					 QWaylandXdgSurface *xdgSurface)
{
	CwlView *view = new CwlView(this, m_workspace->availableGeometry());
	view->setOutput(m_output);
	view->setSurface(xdgSurface->surface());

	view->setTopLevel(toplevel);

	if (m_launcherView != nullptr)
		launcherCloseAnim->start();

	connect(m_workspace, &CwlWorkspace::availableGeometryChanged, view,
		&CwlView::onAvailableGeometryChanged);
	connect(view->surface(), &QWaylandSurface::redraw, view,
		&CwlView::onRedraw);
	connect(view, &QWaylandView::surfaceDestroyed, this,
		&CwlCompositor::viewSurfaceDestroyed);
}

void CwlCompositor::initInputMethod()
{
	if (m_inputMngr != nullptr)
		delete m_inputMngr;

	m_inputMngr = new InputMethodManagerV2(this);
	connect(m_inputMngr, &InputMethodManagerV2::imDestroyed, this,
		&CwlCompositor::initInputMethod);
}

void CwlCompositor::animationValueChanged(const QVariant &value)
{
	triggerRender();
}

void CwlCompositor::onHideKeyboard()
{
	if (m_inputMngr->getInputMethod() != nullptr)
		m_inputMngr->getInputMethod()->hidePanel();
}

void CwlCompositor::onXdgPopupCreated(QWaylandXdgPopup *popup,
				      QWaylandXdgSurface *xdgSurface)
{
	CwlView *view = new CwlView(this, m_workspace->availableGeometry());
	view->setOutput(m_output);
	view->setSurface(xdgSurface->surface());
	view->setPopUp(popup);

	CwlView *parent_view = findTlView(popup->parentXdgSurface()->surface());
	if (parent_view)
		view->setDesktopOutput(parent_view->output(),
				       outputGeometry(parent_view->output()));

	qDebug() << "Popup " << (uint64_t)view << " created";

	view->setPosition(popup->unconstrainedPosition() +
			  parent_view->getPosition());

	view->layer = TOP;
	if (parent_view) {
		parent_view->addChildView(view);
		view->setParentView(parent_view);
		qDebug() << "Popup " << (uint64_t)view << " parented to "
			 << (uint64_t)parent_view;
		connect(view->surface(), &QWaylandSurface::redraw, view,
			&CwlView::onRedraw);
		connect(view, &QWaylandView::surfaceDestroyed, this,
			&CwlCompositor::viewSurfaceDestroyed);
		connect(popup, &QWaylandXdgPopup::configuredGeometryChanged,
			view, &CwlView::onPopUpGeometryChanged);
	} else
		m_workspace->addView(view);
}

void CwlCompositor::onLayerShellSurfaceCreated(LayerSurfaceV1 *layerSurface)
{
	QWaylandOutput *output = layerSurface->output ? layerSurface->output : m_output;
	if (layerSurface->ls_scope.endsWith(QStringLiteral("-external")) &&
	    output == m_output && !m_externalOutputs.isEmpty()) {
		output = m_externalOutputs.constBegin().value();
		layerSurface->output = output;
	}
	CwlView *view = new CwlView(this, outputGeometry(output));
	view->setOutput(output);
	view->setSurface(layerSurface->surface);

	view->setLayerSurface(layerSurface);
	view->setPosition(QPoint(0, 0));
	layerSurface->send_configure(0, 0, 0);

	view->layer = (CwlViewLayer)layerSurface->ls_layer;
	m_workspace->addView(view);

	if (layerSurface->ls_scope == "cutie-home" && output == m_output)
		m_homeView = view;

	if (layerSurface->ls_scope == "cutie-panel" && output == m_output)
		m_panelView = view;
	connect(view->surface(), &QWaylandSurface::redraw, view,
		&CwlView::onRedraw);
	connect(view, &QWaylandView::surfaceDestroyed, this,
		&CwlCompositor::viewSurfaceDestroyed);
	connect(view->m_layerSurface, &LayerSurfaceV1::layerSurfaceDataChanged,
		m_workspace, &CwlWorkspace::onLayerSurfaceDataChanged);
	connect(view->m_layerSurface, &LayerSurfaceV1::hideKeyboard, this,
		&CwlCompositor::onHideKeyboard);
}

CwlView *CwlCompositor::getHomeView()
{
	return m_homeView;
}

CwlView *CwlCompositor::getTopPanel()
{
	return m_panelView;
}

void CwlCompositor::raise(CwlView *view)
{
	m_workspace->raiseView(view);

	if (view->isHidden())
		view->setHidden(false);

	defaultSeat()->setKeyboardFocus(view->surface());

	if (view == m_homeView) {
		m_homeOpen = true;
		unblurAnim->start();
	} else {
		m_homeOpen = false;
		blurAnim->start();
		
 		// Close the launcher when switching focus to an app.
		if (m_launcherView && launcherPosition() > 0.0)
			launcherCloseAnim->start();
	}

	triggerRender();
}

void CwlCompositor::handleTouchEvent(QList<QEventPoint> points,
				     QWaylandOutput *output)
{
	if (points.isEmpty())
		return;
	if (!output)
		output = m_output;
	const qreal outputScale = qMax(1, output->scaleFactor());
	CwlView *view = output == m_output && launcherPosition() != 0.0
			? m_launcherView : viewAt(points.first().position().toPoint(), output);
	if (view == nullptr)
		return;
	foreach(QEventPoint point, points) {
		defaultSeat()->touch()->sendTouchPointEvent(
			view->surface(), point.id(),
			output->position() + point.position() / outputScale -
				view->getPosition(),
			(Qt::TouchPointState)point.state());
	}
	defaultSeat()->touch()->sendFrameEvent(view->surface()->client());
}

void CwlCompositor::handleMouseMoveEvent(QList<QEventPoint> points,
					QWaylandOutput *output)
{
	if (!points.isEmpty())
		handleMouseMoveEvent(points.first().position(), output);
}

void CwlCompositor::handleMouseMoveEvent(const QPointF &position,
					QWaylandOutput *output)
{
	if (!output)
		output = m_output;
	const qreal outputScale = qMax(1, output->scaleFactor());
	CwlView *view = output == m_output && launcherPosition() != 0.0
			? m_launcherView : viewAt(position.toPoint(), output);
	if (view == nullptr)
		return;
	defaultSeat()->sendMouseMoveEvent(
		view, output->position() + position / outputScale -
			      view->getPosition());
}

void CwlCompositor::handleMousePressEvent(QList<QEventPoint> points,
					  Qt::MouseButton btn,
					  QWaylandOutput *output)
{
	if (!points.isEmpty())
		handleMousePressEvent(points.first().position(), btn, output);
}

void CwlCompositor::handleMousePressEvent(const QPointF &position,
					  Qt::MouseButton btn,
					  QWaylandOutput *output)
{
	handleMouseMoveEvent(position, output);
	defaultSeat()->sendMousePressEvent(btn);
}

void CwlCompositor::handleMouseReleaseEvent(QList<QEventPoint> points,
					    Qt::MouseButton btn,
					    QWaylandOutput *output)
{
	if (!points.isEmpty())
		handleMouseReleaseEvent(points.first().position(), btn, output);
}

void CwlCompositor::handleMouseReleaseEvent(const QPointF &position,
					    Qt::MouseButton btn,
					    QWaylandOutput *output)
{
	handleMouseMoveEvent(position, output);
	defaultSeat()->sendMouseReleaseEvent(btn);
}

// Output under a position in the Qt virtual desktop and the position local to
// it. Returns nullptr if no screen, or more than one, contains the position.
QWaylandOutput *CwlCompositor::outputAtGlobal(const QPointF &global,
					      QPointF *local) const
{
	std::pair<QScreen *, QWaylandOutput *> hit{ nullptr, nullptr };
	int matches = 0;
	auto check = [&](QScreen *screen, QWaylandOutput *output) {
		if (screen && output &&
		    screen->geometry().contains(global.toPoint())) {
			hit = { screen, output };
			++matches;
		}
	};
	check(m_glwindow->screen(), m_output);
	for (auto it = m_externalOutputs.cbegin();
	     it != m_externalOutputs.cend(); ++it)
		check(it.key(), it.value());
	if (matches != 1)
		return nullptr;
	*local = global - QPointF(hit.first->geometry().topLeft());
	return hit.second;
}

bool CwlCompositor::handleGesture(QPointerEvent *ev, int edge, int corner)
{
	if (edge == EDGE_LEFT) {
		if (ev->isBeginEvent()) {
			return (launcherPosition() == 0.0) && (blur() != 0.0);
		}

		if (ev->isUpdateEvent()) {
			if ((ev->points().first().globalPosition() -
			     m_glwindow->gesture()->startingPoint())
				    .x() > GESTURE_MINIMUM_THRESHOLD) {
				m_glwindow->gesture()->confirmGesture();
			}
			setBlur(1.0 -
				1.0 * ev->points().first().globalPosition().x() /
					m_glwindow->width());
			return true;
		}

		if (ev->isEndEvent()) {
			if (ev->points().first().globalPosition().x() >
			    GESTURE_ACCEPT_THRESHOLD) {
				raise(m_homeView);
				return true;
			}
			blurAnim->start();
			m_homeOpen = false;
			return true;
		}
	}

	if (edge == EDGE_RIGHT) {
		if (ev->isBeginEvent()) {
			return (launcherPosition() == 0.0) && (blur() != 0.0);
		}

		if (ev->isUpdateEvent()) {
			if ((-ev->points().first().globalPosition() +
			     m_glwindow->gesture()->startingPoint())
				    .x() > GESTURE_MINIMUM_THRESHOLD) {
				m_glwindow->gesture()->confirmGesture();
			}
			setBlur(1.0 *
				ev->points().first().globalPosition().x() /
				m_glwindow->width());
			return true;
		}

		if (ev->isEndEvent()) {
			if (ev->points().first().globalPosition().x() <
			    m_glwindow->width() - GESTURE_ACCEPT_THRESHOLD) {
				raise(m_homeView);
				return true;
			}
			blurAnim->start();
			m_homeOpen = false;
			return true;
		}
	}

	if (edge == EDGE_BOTTOM) {
		if (ev->isBeginEvent() || ev->isUpdateEvent()) {
			if (m_panelView != nullptr)
				if (m_panelView->panelState > 1)
					return false;
			if (m_inputMngr->getInputMethod() != nullptr)
				if (!m_inputMngr->getInputMethod()
					     ->isPanelHidden())
					return false;
			if ((-ev->points().first().globalPosition() +
			     m_glwindow->gesture()->startingPoint())
				    .y() > GESTURE_MINIMUM_THRESHOLD) {
				m_glwindow->gesture()->confirmGesture();
				setLauncherPosition(qMin(
					1.0,
					1.0 - (ev->points().first()
							       .globalPosition()
							       .y() /
						       scaleFactor() -
					       m_workspace->outputGeometry()
						       .y()) /
							m_workspace
								->outputGeometry()
								.height()));
			}
			return true;
		}

		if (ev->isEndEvent()) {
			if (m_panelView != nullptr) {
				if (m_panelView->panelState > 1) {
					return false;
				}
			}
			if (ev->points().first().globalPosition().y() <
			    m_glwindow->height() * 0.8)
				launcherOpenAnim->start();
			else
				launcherCloseAnim->start();
			return true;
		}
	}

	if (edge == EDGE_TOP) {
		if (launcherPosition() > 0.0) {
			if (ev->isBeginEvent() || ev->isUpdateEvent()) {
				if ((ev->points().first().globalPosition() -
				     m_glwindow->gesture()->startingPoint())
					    .y() > GESTURE_MINIMUM_THRESHOLD) {
					m_glwindow->gesture()->confirmGesture();
				}
				setLauncherPosition(qMin(
					1.0,
					1.0 - (ev->points().first()
							       .globalPosition()
							       .y() /
						       scaleFactor() -
					       m_workspace->outputGeometry()
						       .y()) /
							m_workspace
								->outputGeometry()
								.height()));
			}

			if (ev->isEndEvent()) {
				if (ev->points().first().globalPosition().y() <
				    m_glwindow->height() * 0.2)
					launcherOpenAnim->start();
				else
					launcherCloseAnim->start();
			}
			return true;
		}
	}

	if (corner == CORNER_BR || corner == CORNER_BL) {
		if (launcherPosition() > 0.0) {
			return false;
		}

		if (m_panelView != nullptr)
			if (m_panelView->panelState > 1)
				return false;

		if (ev->isBeginEvent() || ev->isUpdateEvent()) {
			if (m_inputMngr->getInputMethod() != nullptr)
				if (!m_inputMngr->getInputMethod()
					     ->isPanelHidden()) {
					return false;
				}
			if ((-ev->points().first().globalPosition() +
			     m_glwindow->gesture()->startingPoint())
				    .y() > GESTURE_MINIMUM_THRESHOLD) {
				m_glwindow->gesture()->confirmGesture();
			}
			return true;
		}

		if (ev->isEndEvent()) {
			if (m_panelView != nullptr) {
				if (m_panelView->panelState > 1) {
					return false;
				}
			}
			if (ev->points().first().globalPosition().y() <
			    m_glwindow->height() * 0.8) {
				m_inputMngr->getInputMethod()->showPanel();
				return true;
			}
		}
	}

	return false;
}

void CwlCompositor::viewSurfaceDestroyed()
{
	CwlView *view = qobject_cast<CwlView *>(sender());
	CwlView *parent = view->parentView();

	if (parent != nullptr) {
		if (!view->isPopup())
			raise(parent);
		else
			view->getPopup()->sendPopupDone();

		parent->removeChildView(view);
	} else if (view == m_launcherView) {
		m_launcherView = nullptr;
	} else {
		m_workspace->removeView(view);
	}

	delete view;
	triggerRender();
}

void CwlCompositor::handleKeyPress(quint32 nativeScanCode)
{
	defaultSeat()->sendKeyPressEvent(nativeScanCode);
}

void CwlCompositor::handleKeyRelease(quint32 nativeScanCode)
{
	defaultSeat()->sendKeyReleaseEvent(nativeScanCode);
}

void CwlCompositor::updateCursor(const QPointF &position, QWaylandOutput *output)
{
	if (!output)
		output = m_output;
	if (output != m_cursorOutput)
		qInfo() << "Cutie: pointer entered"
			<< (output == m_output ? "primary" : "external")
			<< "output at" << position;
	m_cursorPos = position;
	m_cursorOutput = output;
	m_mouseActive = true;
	m_mouseInactivityTimer->start();
	triggerRender();
}

void CwlCompositor::triggerRender()
{
	m_glwindow->requestUpdate();
	for (GlWindow *window : m_externalWindows)
		window->requestUpdate();
}

void CwlCompositor::onToplevelDamaged(CwlView *view)
{
	if (!m_homeOpen)
        return;

	m_cutieshell->onThumbnailDamage(view);
}

void CwlCompositor::startRender(QWaylandOutput *output)
{
	if (output)
		output->frameStarted();
}

void CwlCompositor::endRender(QWaylandOutput *output)
{
	if (output)
		output->sendFrameCallbacks();
}

int CwlCompositor::scaleFactor()
{
	return m_scaleFactor;
}

void CwlCompositor::setScaleFactor(int scale)
{
	if (m_scaleFactor == scale)
		return;
	m_scaleFactor = scale;
	if (m_output)
		m_output->setScaleFactor(m_scaleFactor);
	emit scaleFactorChanged(m_scaleFactor);
}

GlWindow *CwlCompositor::glWindow()
{
	return m_glwindow;
}

double CwlCompositor::blur()
{
	return m_blur;
}

void CwlCompositor::setBlur(double blur)
{
	if (m_blur == blur)
		return;
	m_blur = blur;
	emit blurChanged(m_blur);
}

double CwlCompositor::launcherPosition()
{
	return m_launcherPosition;
}

void CwlCompositor::setLauncherPosition(double position)
{
	if (m_launcherPosition == position)
		return;
	m_launcherPosition = position;
	QPointF newPos = m_launcherView->getPosition();
	newPos.setY((1.0 - m_launcherPosition) *
			    m_workspace->outputGeometry().height() +
		    m_workspace->outputGeometry().y());
	if (newPos.y() < m_workspace->availableGeometry().y() -
				 m_workspace->outputGeometry().y())
		newPos.setY(m_workspace->availableGeometry().y() -
			    m_workspace->outputGeometry().y());
	if (m_homeOpen)
		setBlur(m_launcherPosition);
	m_launcherView->setPosition(newPos);
	emit launcherPositionChanged(m_launcherPosition);
}

ForeignToplevelManagerV1 *CwlCompositor::foreignTlManagerV1()
{
	return m_foreignTlManagerV1;
}

void CwlCompositor::grabSurface(QWaylandSurfaceGrabber *grabber,
				const QWaylandBufferRef &buffer)
{
	if (buffer.isSharedMemory()) {
		emit grabber->success(buffer.image());
	} else {
		if (QOpenGLContext::currentContext()) {
			QOpenGLFramebufferObject fbo(buffer.size());
			fbo.bind();
			QOpenGLTextureBlitter blitter;
			blitter.create();

			glViewport(0, 0, buffer.size().width(),
				   buffer.size().height());
			glClearColor(0.f, 0.f, 0.f, 0.f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			QOpenGLTextureBlitter::Origin surfaceOrigin =
				buffer.origin() ==
						QWaylandSurface::OriginTopLeft ?
					QOpenGLTextureBlitter::OriginTopLeft :
					QOpenGLTextureBlitter::OriginBottomLeft;

			auto texture = buffer.toOpenGLTexture();
			blitter.bind(texture->target());
			blitter.blit(texture->textureId(), QMatrix4x4(),
				     surfaceOrigin);
			blitter.release();

			emit grabber->success(fbo.toImage());
		} else
			emit grabber->failed(
				QWaylandSurfaceGrabber::UnknownBufferType);
	}
}
