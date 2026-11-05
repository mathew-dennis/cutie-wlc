#include <glwindow.h>
#include <decoration.h>

#include <QPainter>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QDebug>
#include <QSet>
#include <wayland-server-core.h>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformscreen.h>
#include <QtWaylandCompositor/QWaylandSeat>

GlWindow::GlWindow()
{
	QGuiApplication::platformNativeInterface()->nativeResourceForIntegration(
		"displayon");
	m_displayOff = false;
}

void GlWindow::setCompositor(CwlCompositor *cwlcompositor)
{
	m_cwlcompositor = cwlcompositor;
	if (m_gesture)
		delete m_gesture;
	m_gesture = new CwlGesture(cwlcompositor, QSize(width(), height()));
}

void GlWindow::setOutput(QWaylandOutput *output)
{
	m_output = output;
	m_externalRenderLogged = false;
	requestUpdate();
}

QWaylandOutput *GlWindow::output() const
{
	return m_output;
}

bool GlWindow::displayOff()
{
	return m_displayOff;
}

void GlWindow::setDisplayOff(bool displayOff)
{
	if (m_displayOff == displayOff)
		return;

	QGuiApplication::primaryScreen()->handle()->setPowerState(
		displayOff ? QPlatformScreen::PowerStateOff :
			     QPlatformScreen::PowerStateOn);

	if (displayOff) {
		m_cwlcompositor->setLauncherPosition(0.0);
		m_cwlcompositor->onHideKeyboard();
	} else
		requestUpdate();

	m_displayOff = displayOff;
	emit displayOffChanged(m_displayOff);
}

void GlWindow::initializeGL()
{
	m_textureBlitter.create();

	// Arrow used when no client cursor surface is set.
	QImage arrow(24, 24, QImage::Format_ARGB32);
	arrow.fill(Qt::transparent);
	QPainter painter(&arrow);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(Qt::black, 1.2));
	painter.setBrush(Qt::white);
	painter.drawPolygon(QPolygonF({ QPointF(1, 1), QPointF(1, 18),
				       QPointF(5.5, 14), QPointF(8.5, 21),
				       QPointF(11.5, 19.7), QPointF(8.5, 13),
				       QPointF(14, 13) }));
	painter.end();
	m_fallbackCursor =
		new QOpenGLTexture(arrow.convertToFormat(QImage::Format_RGBA8888));

	// Resize pointers and the window border colors
	for (int shape = CwlDecoration::CursorResizeH;
	     shape < CwlDecoration::CursorShapeCount; ++shape)
		m_shapeCursors[shape] = new QOpenGLTexture(
			CwlDecoration::cursorImage(shape));
	for (int active = 0; active < 2; ++active) {
		QImage color(1, 1, QImage::Format_ARGB32);
		color.fill(CwlDecoration::borderColor(active));
		m_borderTextures[active] =
			new QOpenGLTexture(color.convertToFormat(
				QImage::Format_RGBA8888));
	}

	emit glReady();
}

void GlWindow::paintGL()
{
	if (m_displayOff || !m_output)
		return;
	qDeleteAll(m_staleTextures);
	m_staleTextures.clear();
	m_cwlcompositor->startRender(m_output);
	const bool primaryOutput = m_output == m_cwlcompositor->defaultOutput();

	QOpenGLFunctions *functions = context()->functions();

	if (!primaryOutput && !m_externalRenderLogged) {
		GLint viewport[4] = {};
		functions->glGetIntegerv(GL_VIEWPORT, viewport);

		qInfo() << "Cutie external rendering:"
			<< "Screen:" << screen()->name()
			<< "Screen geometry:" << screen()->geometry()
			<< "Screen DPR:" << screen()->devicePixelRatio()
			<< "Window size:" << size()
			<< "Window DPR:" << devicePixelRatio()
			<< "Framebuffer:" << viewport[2] << "x" << viewport[3]
			<< "Wayland mode:" << m_output->currentMode().size()
			<< "Wayland scale:" << m_output->scaleFactor();

		m_externalRenderLogged = true;
	}

	functions->glClearColor(.0f, .0f, .0f, 1.f);
	functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	m_currentTarget = GL_TEXTURE_2D;
	m_textureBlitter.bind(m_currentTarget);
	functions->glEnable(GL_BLEND);
	functions->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	QList<CwlView *> renderViews = m_cwlcompositor->getViews();
	if (primaryOutput && m_cwlcompositor->m_launcherView)
		renderViews << m_cwlcompositor->m_launcherView;

	for (CwlView *view : renderViews) {
		if (!view || view->output() != m_output)
			continue;
		QString appId;
		if (view->isToplevel())
			appId = view->getAppId();

		if (!primaryOutput)
			m_textureBlitter.setOpacity(1.0);
		else if (appId == "cutie-launcher")
			m_textureBlitter.setOpacity(
				1.0 -
				(m_cwlcompositor->m_launcherView->getPosition()
					 .y() *
				 m_cwlcompositor->scaleFactor() / height()));
		else if (view->isToplevel())
			if (m_cwlcompositor->launcherPosition() > 0.0)
				m_textureBlitter.setOpacity(
					m_cwlcompositor->blur() *
					m_cwlcompositor->m_launcherView
						->getPosition()
						.y() *
					m_cwlcompositor->scaleFactor() /
					height());
			else
				m_textureBlitter.setOpacity(
					m_cwlcompositor->blur());
		else
			m_textureBlitter.setOpacity(1.0);

		if (primaryOutput && m_cwlcompositor->launcherPosition() == 1.0 &&
		    view->layer == CwlViewLayer::TOP)
			continue;

		if (!primaryOutput && view->hasTitleBar()) {
			renderBorder(view, m_cwlcompositor->isActiveWindow(view));
			renderTitleBar(view);
		}
		renderView(view);
	}

	renderCursor();

	m_textureBlitter.release();
	m_cwlcompositor->endRender(m_output);
}

void GlWindow::renderBorder(CwlView *view, bool active)
{
	// A maximized window fills the area, nothing to frame
	if (view->isWindowMaximized())
		return;
	const int outputScale = qMax(1, m_output->scaleFactor());
	const qreal b = CwlDecoration::BorderWidth;
	const QRectF f = CwlDecoration::frameRect(
		QRectF(view->getPosition(), QSizeF(view->size())));
	const QRectF edges[4] = {
		QRectF(f.left() - b, f.top() - b, f.width() + 2 * b, b),
		QRectF(f.left() - b, f.bottom(), f.width() + 2 * b, b),
		QRectF(f.left() - b, f.top(), b, f.height()),
		QRectF(f.right(), f.top(), b, f.height())
	};

	QOpenGLTexture *texture = m_borderTextures[active ? 1 : 0];
	if (texture->target() != m_currentTarget) {
		m_currentTarget = texture->target();
		m_textureBlitter.bind(m_currentTarget);
	}
	m_textureBlitter.setOpacity(1.0);
	for (const QRectF &edge : edges) {
		const QRectF target((edge.topLeft() - m_output->position()) *
					    outputScale,
				    edge.size() * outputScale);
		m_textureBlitter.blit(texture->textureId(),
				      QOpenGLTextureBlitter::targetTransform(
					      target, QRect(QPoint(), size())),
				      QOpenGLTextureBlitter::OriginTopLeft);
	}
}

void GlWindow::renderTitleBar(CwlView *view)
{
	const int outputScale = qMax(1, m_output->scaleFactor());
	const QRectF bar = CwlDecoration::titleBarRect(
		QRectF(view->getPosition(), QSizeF(view->size())));
	const QSize pixels = (bar.size() * outputScale).toSize();
	if (pixels.isEmpty())
		return;

	CwlCompositor *c = m_cwlcompositor;
	const QString title = view->getTitle();
	const bool active = c->isActiveWindow(view);
	const bool maximized = view->isWindowMaximized();
	const int hover = c->m_hoverView == view ? c->m_hoverPart : 0;

	if (!m_titleBars.contains(view)) {
		// The texture belongs to this window's context; free it later
		connect(view, &QObject::destroyed, this, [this, view]() {
			QOpenGLTexture *old = m_titleBars.take(view).texture;
			if (old)
				m_staleTextures << old;
		});
	}
	TitleBarTexture &entry = m_titleBars[view];
	if (!entry.texture || entry.size != pixels || entry.title != title ||
	    entry.active != active || entry.maximized != maximized ||
	    entry.hover != hover) {
		delete entry.texture;
		entry.texture = new QOpenGLTexture(CwlDecoration::paintTitleBar(
			pixels, outputScale, title, active, maximized, hover));
		entry.size = pixels;
		entry.title = title;
		entry.active = active;
		entry.maximized = maximized;
		entry.hover = hover;
	}

	if (entry.texture->target() != m_currentTarget) {
		m_currentTarget = entry.texture->target();
		m_textureBlitter.bind(m_currentTarget);
	}
	m_textureBlitter.setOpacity(1.0);
	const QRectF target((bar.topLeft() - m_output->position()) * outputScale,
			    QSizeF(pixels));
	m_textureBlitter.blit(entry.texture->textureId(),
			      QOpenGLTextureBlitter::targetTransform(
				      target, QRect(QPoint(), size())),
			      QOpenGLTextureBlitter::OriginTopLeft);
}

void GlWindow::renderCursor()
{
	CwlCompositor *c = m_cwlcompositor;
	if (!c->m_mouseActive || c->m_cursorOutput != m_output)
		return;
	const int shape = c->m_cursorShape;
	if (shape == CwlDecoration::CursorClient && c->m_cursorHidden)
		return;

	const int outputScale = qMax(1, m_output->scaleFactor());
	QOpenGLTexture *texture = nullptr;
	QSizeF cursorSize(24, 24);
	QPointF topLeft = c->m_cursorPos;
	auto origin = QOpenGLTextureBlitter::OriginTopLeft;

	CwlView *view = c->m_cursorView;
	if (shape != CwlDecoration::CursorClient) {
		texture = shape == CwlDecoration::CursorArrow ?
				  m_fallbackCursor :
				  m_shapeCursors[shape];
		topLeft -= QPointF(CwlDecoration::cursorHotspot(shape));
	} else if (view && view->surface() &&
		   view->surface()->hasContent()) {
		view->setOutput(m_output);
		texture = view->getTexture();
		if (texture) {
			cursorSize = QSizeF(view->size() * outputScale);
			topLeft -= QPointF(c->m_cursorHotspot * outputScale);
			origin = view->textureOrigin();
		}
	}
	if (!texture)
		texture = m_fallbackCursor;
	if (!texture)
		return;

	if (texture->target() != m_currentTarget) {
		m_currentTarget = texture->target();
		m_textureBlitter.bind(m_currentTarget);
	}
	m_textureBlitter.setOpacity(1.0);
	m_textureBlitter.blit(texture->textureId(),
			      QOpenGLTextureBlitter::targetTransform(
				      QRectF(topLeft, cursorSize),
				      QRect(QPoint(), size())),
			      origin);
}

// The gesture engine only handles pointer events of the primary output, on the
// primary window.
bool GlWindow::ownsPointer(QWaylandOutput *output) const
{
	QWaylandOutput *primary = m_cwlcompositor->defaultOutput();
	return output == primary && m_output == primary &&
	       !m_cwlcompositor->windowGrabActive();
}

// Output under the pointer and the position local to it, taken from the global
// position so it stays right if Qt delivered the event to another window (for
// example while a button is held). Falls back to this window's own output.
QWaylandOutput *GlWindow::pointerTarget(const QMouseEvent *ev,
					QPointF *local) const
{
	*local = ev->position();
	QWaylandOutput *output =
		m_cwlcompositor->outputAtGlobal(ev->globalPosition(), local);
	return output ? output : m_output;
}

void GlWindow::trackCursor(const QMouseEvent *ev, const QPointF &local,
			   QWaylandOutput *output)
{
	// Mouse events synthesized from touch must not show the pointer.
	if (ev->source() == Qt::MouseEventNotSynthesized)
		m_cwlcompositor->updateCursor(local, output);
}

void GlWindow::renderView(CwlView *view)
{
	QOpenGLTexture *texture = view->getTexture();
	if (!texture)
		return;
	if (texture->target() != m_currentTarget) {
		m_currentTarget = texture->target();
		m_textureBlitter.bind(m_currentTarget);
	}

	QWaylandSurface *surface = view->surface();
	if (surface && surface->hasContent()) {
		QSize viewSize = view->size();
		QPointF viewPosition = view->getPosition() - m_output->position();
		auto surfaceOrigin = view->textureOrigin();
		const int outputScale = qMax(1, m_output->scaleFactor());
		QRectF targetRect(viewPosition * outputScale, viewSize * outputScale);

		if (m_output != m_cwlcompositor->defaultOutput()) {
			static QSet<QString> seen;
			const QSize buf = surface->bufferSize();
			const QString key = QString("%1 wl_surface#%2 buf %3x%4 bufScale %5 dest %6x%7 -> target %8x%9")
				.arg(view->getAppId())
				.arg(wl_resource_get_id(surface->resource()))
				.arg(buf.width()).arg(buf.height())
				.arg(surface->bufferScale())
				.arg(viewSize.width()).arg(viewSize.height())
				.arg(qRound(targetRect.width()))
				.arg(qRound(targetRect.height()));
			if (!seen.contains(key)) {
				seen.insert(key);
				qInfo() << "Cutie view blit:" << key;
			}
		}

		QMatrix4x4 targetTransform =
			QOpenGLTextureBlitter::targetTransform(
				targetRect, QRect(QPoint(), size()));
		m_textureBlitter.blit(texture->textureId(), targetTransform,
				      surfaceOrigin);
	}

	if (view->getChildViews().size() > 0) {
		for (CwlView *childView : view->getChildViews())
			renderView(childView);
	}
}

void GlWindow::touchEvent(QTouchEvent *ev)
{
	if (m_output && m_output != m_cwlcompositor->defaultOutput()) {
		m_cwlcompositor->handleTouchEvent(ev->points(), m_output);
		return;
	}
	m_gesture->handlePointerEvent(ev, [this](QList<QEventPoint> points) {
		m_cwlcompositor->handleTouchEvent(points, m_output);
	});
}

void GlWindow::mouseMoveEvent(QMouseEvent *ev)
{
	QPointF local;
	QWaylandOutput *output = pointerTarget(ev, &local);
	trackCursor(ev, local, output);
	if (!ownsPointer(output)) {
		m_cwlcompositor->handleMouseMoveEvent(local, output);
		return;
	}
	m_gesture->handlePointerEvent(ev, [this](QList<QEventPoint> points) {
		m_cwlcompositor->handleMouseMoveEvent(points, m_output);
	});
}

void GlWindow::mousePressEvent(QMouseEvent *ev)
{
	QPointF local;
	QWaylandOutput *output = pointerTarget(ev, &local);
	trackCursor(ev, local, output);
	Qt::MouseButton btn = ev->button();
	if (!ownsPointer(output)) {
		m_cwlcompositor->handleMousePressEvent(local, btn, output);
		return;
	}
	m_gesture->handlePointerEvent(ev, [this,
					   btn](QList<QEventPoint> points) {
		m_cwlcompositor->handleMousePressEvent(points, btn, m_output);
	});
}

void GlWindow::mouseReleaseEvent(QMouseEvent *ev)
{
	QPointF local;
	QWaylandOutput *output = pointerTarget(ev, &local);
	trackCursor(ev, local, output);
	Qt::MouseButton btn = ev->button();
	if (!ownsPointer(output)) {
		m_cwlcompositor->handleMouseReleaseEvent(local, btn, output);
		return;
	}
	m_gesture->handlePointerEvent(ev, [this,
					   btn](QList<QEventPoint> points) {
		m_cwlcompositor->handleMouseReleaseEvent(points, btn, m_output);
	});
}

// Shell keys are consumed by the shell. Auto-repeat is left to the clients
// (wl_keyboard.repeat_info), so repeated events from Qt are dropped.
static bool isForwardedKey(const QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_PowerOff:
	case Qt::Key_VolumeUp:
	case Qt::Key_VolumeDown:
		return false;
	}
	return !event->isAutoRepeat() && event->nativeScanCode() != 0;
}

void GlWindow::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_PowerOff)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::POWER_PRESS);

	if (event->key() == Qt::Key_VolumeUp)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::VOLUME_UP_PRESS);

	if (event->key() == Qt::Key_VolumeDown)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::VOLUME_DOWN_PRESS);

	if (event->key() == Qt::Key_Tab && (event->modifiers() & Qt::AltModifier)) {
		if (!event->isAutoRepeat())
			m_cwlcompositor->cycleWindows();
		return;
	}

	if (!isForwardedKey(event))
		return;
	m_cwlcompositor->handleKeyPress(event->nativeScanCode());
}

void GlWindow::keyReleaseEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_PowerOff)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::POWER_RELEASE);

	if (event->key() == Qt::Key_VolumeUp)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::VOLUME_UP_RELEASE);

	if (event->key() == Qt::Key_VolumeDown)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::VOLUME_DOWN_RELEASE);

	if (event->key() == Qt::Key_Tab && (event->modifiers() & Qt::AltModifier))
		return;

	if (!isForwardedKey(event))
		return;
	m_cwlcompositor->handleKeyRelease(event->nativeScanCode());
}