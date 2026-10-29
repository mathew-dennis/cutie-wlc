#include <cutie-wlc.h>
#include <glwindow.h>
#include <output-management-v1.h>
#include <output-power-management-v1.h>
#include <screencopy.h>
#include <foreign-toplevel-management.h>
#include <input-method-v2.h>

#include <QtWaylandCompositor/QWaylandSeat>
#include <QWaylandPointer>
#include <QWaylandTouch>
#include <QTouchEvent>
#include <QScreen>
#include <QElapsedTimer>
#include <QDateTime>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>

// Temporary frame timing
static QElapsedTimer s_clock;
static qint64 s_paintStartNs = 0, s_paintEndNs = 0, s_maxSwapNs = 0;
static int s_updateRequests = 0;

CwlCompositor::CwlCompositor(GlWindow *glwindow)
	: m_glwindow(glwindow)
	, m_xdgShell(new QWaylandXdgShell(this))
	, m_layerShell(new LayerShellV1(this))
	, m_xdgdecoration(new QWaylandXdgDecorationManagerV1())
{
	m_glwindow->setCompositor(this);
	connect(m_glwindow, &GlWindow::glReady, this, &CwlCompositor::create);

	connect(m_xdgShell, &QWaylandXdgShell::toplevelCreated, this,
		&CwlCompositor::onXdgToplevelCreated);
	connect(m_xdgShell, &QWaylandXdgShell::popupCreated, this,
		&CwlCompositor::onXdgPopupCreated);
	connect(m_layerShell, &LayerShellV1::layerShellSurfaceCreated, this,
		&CwlCompositor::onLayerShellSurfaceCreated);

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
}

void CwlCompositor::create()
{
	m_output = new QWaylandOutput(this, m_glwindow);
	qreal hz = m_glwindow->screen()->refreshRate();
	QWaylandOutputMode mode(m_glwindow->size(),
				hz >= 1.0 ? qRound(hz * 1000) : 60000);
	qDebug("CUTIE_PERF src=wlc ev=output_mode screen_hz=%.2f mode_mhz=%d",
	       (double)hz, (int)mode.refreshRate());
	m_output->addMode(mode, true);
	QWaylandCompositor::create();
	connect(m_glwindow, &QOpenGLWindow::frameSwapped, this, [] {
		s_maxSwapNs = qMax(s_maxSwapNs,
				   s_clock.nsecsElapsed() - s_paintEndNs);
	});
	m_output->setCurrentMode(mode);
	m_output->setScaleFactor(m_scaleFactor);

	m_xdgdecoration->setExtensionContainer(this);
	m_xdgdecoration->initialize();
	m_xdgdecoration->setPreferredMode(
		QWaylandXdgToplevel::ServerSideDecoration);

	m_workspace = new CwlWorkspace(this);
	m_cutieshell = new CutieShell(this);
	m_outputManager = new OutputManagerV1(this);
	m_outputPowerManager = new OutputPowerManagerV1(this);
	m_screencopyManager = new ScreencopyManagerV1(this);

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
	// Set CUTIE_CLIENT_VSYNC=1 for the compositor to let clients pace
	// themselves on frame callbacks instead of free-running.
	if (qgetenv("CUTIE_CLIENT_VSYNC") != "1")
		qputenv("QSG_NO_VSYNC", QByteArray("1"));
	qputenv("QSG_RENDER_LOOP", QByteArray("threaded"));
	qDebug("CUTIE_PERF src=wlc ev=client_env qsg_no_vsync=\"%s\" render_loop=\"%s\"",
	       qgetenv("QSG_NO_VSYNC").constData(),
	       qgetenv("QSG_RENDER_LOOP").constData());

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

CwlView *CwlCompositor::viewAt(const QPoint &position)
{
	CwlView *ret = nullptr;
	for (auto it = getViews().crbegin(); it != getViews().crend(); ++it) {
		CwlView *view = *it;
		QRectF geom(view->getPosition(), view->size() * scaleFactor());
		QPoint checkPoint = position;

		if (view->getAppId() == "cutie-keyboard")
			checkPoint = position / scaleFactor();

		if (geom.contains(checkPoint)) {
			if (view->getChildViews().size() > 0) {
				for (CwlView *childView :
				     view->getChildViews()) {
					checkPoint = position / scaleFactor();
					QRectF geom(childView->getPosition(),
						    childView->size() *
							    scaleFactor());
					if (geom.contains(checkPoint)) {
						ret = childView;
						return ret;
					}
				}
			}
			ret = view;
			return ret;
		}
	}
	return ret;
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
	view->setOutput(outputFor(m_glwindow));
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
	view->setOutput(outputFor(m_glwindow));
	view->setSurface(xdgSurface->surface());
	view->setPopUp(popup);

	CwlView *parent_view = findTlView(popup->parentXdgSurface()->surface());

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
	CwlView *view = new CwlView(this, m_workspace->availableGeometry());
	view->setOutput(outputFor(m_glwindow));
	view->setSurface(layerSurface->surface);

	view->setLayerSurface(layerSurface);
	view->setPosition(QPoint(0, 0));
	layerSurface->send_configure(0, 0, 0);

	view->layer = (CwlViewLayer)layerSurface->ls_layer;
	m_workspace->addView(view);

	if (layerSurface->ls_scope == "cutie-home")
		m_homeView = view;

	if (layerSurface->ls_scope == "cutie-panel")
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

void CwlCompositor::handleTouchEvent(QList<QEventPoint> points)
{
	CwlView *view = m_launcherView;
	if (launcherPosition() == 0.0)
		view = viewAt(points.first().globalPosition().toPoint());
	if (view == nullptr)
		return;
	foreach(QEventPoint point, points) {
		defaultSeat()->touch()->sendTouchPointEvent(
			view->surface(), point.id(),
			point.position() / scaleFactor() - view->getPosition(),
			(Qt::TouchPointState)point.state());
	}
	defaultSeat()->touch()->sendFrameEvent(view->surface()->client());
}

void CwlCompositor::handleMouseMoveEvent(QList<QEventPoint> points)
{
	CwlView *view = m_launcherView;
	if (launcherPosition() == 0.0)
		view = viewAt(points.first().position().toPoint());
	if (view == nullptr)
		return;
	defaultSeat()->sendMouseMoveEvent(
		view, points.first().position().toPoint() / scaleFactor() -
			      view->getPosition());
}

void CwlCompositor::handleMousePressEvent(QList<QEventPoint> points,
					  Qt::MouseButton btn)
{
	handleMouseMoveEvent(points);
	defaultSeat()->sendMousePressEvent(btn);
}

void CwlCompositor::handleMouseReleaseEvent(QList<QEventPoint> points,
					    Qt::MouseButton btn)
{
	handleMouseMoveEvent(points);
	defaultSeat()->sendMouseReleaseEvent(btn);
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

void CwlCompositor::triggerRender()
{
	s_updateRequests++;
	m_glwindow->requestUpdate();
}

void CwlCompositor::onToplevelDamaged(CwlView *view)
{
	if (!m_homeOpen)
        return;

	m_cutieshell->onThumbnailDamage(view);
}

void CwlCompositor::startRender()
{
	if (!s_clock.isValid())
		s_clock.start();
	s_paintStartNs = s_clock.nsecsElapsed();

	QWaylandOutput *out = defaultOutput();
	if (out)
		out->frameStarted();
}

void CwlCompositor::endRender()
{
	QWaylandOutput *out = defaultOutput();
	if (out)
		out->sendFrameCallbacks();

	// Temporary: log how fast the compositor itself paints
	static qint64 lastNs = 0, startNs = 0, maxGapNs = 0, maxPaintNs = 0;
	static int frames = 0;
	qint64 now = s_clock.nsecsElapsed();
	// Gaps over 1 s are idle (nothing to paint), not stalls
	if (lastNs && now - lastNs < 1000000000LL)
		maxGapNs = qMax(maxGapNs, now - lastNs);
	maxPaintNs = qMax(maxPaintNs, now - s_paintStartNs);
	s_paintEndNs = now;
	lastNs = now;
	frames++;
	if (now - startNs >= 1000000000LL) {
		qDebug("CUTIE_PERF src=wlc ev=paint ts_ms=%lld paint_fps=%d "
		       "worst_gap_ms=%.1f worst_paintgl_ms=%.1f worst_swap_ms=%.1f "
		       "update_requests=%d",
		       (long long)QDateTime::currentMSecsSinceEpoch(), frames,
		       maxGapNs / 1e6, maxPaintNs / 1e6, s_maxSwapNs / 1e6,
		       s_updateRequests);
		frames = 0;
		maxGapNs = 0;
		maxPaintNs = 0;
		s_maxSwapNs = 0;
		s_updateRequests = 0;
		startNs = now;
	}
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
