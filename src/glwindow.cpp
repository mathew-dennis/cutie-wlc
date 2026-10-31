#include <glwindow.h>

#include <QPainter>
#include <QPainterPath>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QCursor>
#include <QPixmap>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGuiApplication>
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

	// Build the software cursor texture for this window up front, in
	// its own GL context — the same mechanism the working pointer on
	// the multi-desktop branch uses. Creating it lazily from inside
	// paintGL() left the big screen without a usable texture.
	QImage cursorImage;
	QPixmap systemArrow = QCursor(Qt::ArrowCursor).pixmap();
	if (!systemArrow.isNull())
		cursorImage =
			systemArrow.toImage().convertToFormat(QImage::Format_RGBA8888);
	if (cursorImage.isNull()) {
		cursorImage = QImage(24, 24, QImage::Format_RGBA8888);
		cursorImage.fill(Qt::transparent);
		QPainter painter(&cursorImage);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(QPen(Qt::black, 1));
		painter.setBrush(Qt::white);
		QPainterPath arrow;
		arrow.moveTo(1.5, 1.5);
		arrow.lineTo(1.5, 16.5);
		arrow.lineTo(5.5, 12.5);
		arrow.lineTo(8.5, 19.5);
		arrow.lineTo(10.5, 18.5);
		arrow.lineTo(7.5, 11.5);
		arrow.lineTo(12.5, 11.5);
		arrow.closeSubpath();
		painter.drawPath(arrow);
		painter.end();
	}
	m_cursorTexture = new QOpenGLTexture(cursorImage);
	m_cursorTexture->create();

	emit glReady();
}

void GlWindow::paintGL()
{
	if (m_displayOff || !m_output)
		return;
	m_cwlcompositor->startRender(m_output);
	const bool primaryOutput = m_output == m_cwlcompositor->defaultOutput();

	QOpenGLFunctions *functions = context()->functions();
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
		// Minimized windows are not rendered.
		if (view->isMinimized())
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

		renderView(view);
	}

	// The big screen has no hardware cursor; draw the software cursor
	// on top of the views while the blitter is still bound.
	if (!primaryOutput)
		renderCursor();

	m_textureBlitter.release();
	m_cwlcompositor->endRender(m_output);
}

void GlWindow::renderView(CwlView *view)
{
	if (view->isMinimized())
		return;
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
    if (m_output && m_output != m_cwlcompositor->defaultOutput()) {
        // Track the pointer for the software cursor before routing
        // the event to the client underneath, and repaint right away
        // so the cursor keeps up with the mouse instead of waiting
        // for the next client commit.
        m_cursorPosition =
            ev->position() / qMax(1, m_output->scaleFactor());
        m_cursorVisible = true;
        m_cwlcompositor->handleMouseMoveEvent(ev->points(), m_output);
        m_cwlcompositor->triggerRender();
        return;
    }
    m_gesture->handlePointerEvent(ev, [this](QList<QEventPoint> points) {
        m_cwlcompositor->handleMouseMoveEvent(points, m_output);
    });
}

void GlWindow::mousePressEvent(QMouseEvent *ev)
{
	Qt::MouseButton btn = ev->button();
	if (m_output && m_output != m_cwlcompositor->defaultOutput()) {
		// Clicks land on the software cursor position too, so the
		// cursor stays glued to the pointer even without motion.
		m_cursorPosition =
			ev->position() / qMax(1, m_output->scaleFactor());
		m_cursorVisible = true;
		m_cwlcompositor->handleMousePressEvent(ev->points(), btn,
						       m_output);
		return;
	}
	m_gesture->handlePointerEvent(ev, [this,
					   btn](QList<QEventPoint> points) {
		m_cwlcompositor->handleMousePressEvent(points, btn, m_output);
	});
}

void GlWindow::mouseReleaseEvent(QMouseEvent *ev)
{
	Qt::MouseButton btn = ev->button();
	if (m_output && m_output != m_cwlcompositor->defaultOutput()) {
		m_cwlcompositor->handleMouseReleaseEvent(ev->points(), btn, m_output);
		return;
	}
	m_gesture->handlePointerEvent(ev, [this,
					   btn](QList<QEventPoint> points) {
		m_cwlcompositor->handleMouseReleaseEvent(points, btn, m_output);
	});
}

void GlWindow::keyPressEvent(QKeyEvent *event)
{
	// Window management shortcuts on the big screen; anything else
	// (power, volume, ...) still falls through to the handlers below.
	if (m_output && m_output != m_cwlcompositor->defaultOutput()) {
		if (m_cwlcompositor->handleExternalKeyShortcut(event))
			return;
		// Regular typing from the big screen goes to the focused
		// client; system keys are filtered out and handled below.
		m_cwlcompositor->forwardKeyEvent(event);
	}

	if (event->key() == Qt::Key_PowerOff)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::POWER_PRESS);

	if (event->key() == Qt::Key_VolumeUp)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::VOLUME_UP_PRESS);

	if (event->key() == Qt::Key_VolumeDown)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::VOLUME_DOWN_PRESS);		
}

void GlWindow::keyReleaseEvent(QKeyEvent *event)
{
	// Key releases from the big screen must reach the focused client as
	// well, or clients are left with stuck keys. forwardKeyEvent()
	// filters out the system keys handled below.
	if (m_output && m_output != m_cwlcompositor->defaultOutput())
		m_cwlcompositor->forwardKeyEvent(event);

	if (event->key() == Qt::Key_PowerOff)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::POWER_RELEASE);

	if (event->key() == Qt::Key_VolumeUp)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::VOLUME_UP_RELEASE);

	if (event->key() == Qt::Key_VolumeDown)
		m_cwlcompositor->specialKey(
			CutieShell::SpecialKey::VOLUME_DOWN_RELEASE);
}

void GlWindow::wheelEvent(QWheelEvent *ev)
{
	// Scroll events only arrive from the big screen; the phone UI
	// handles scrolling through touch events inside the clients.
	if (!m_output || m_output == m_cwlcompositor->defaultOutput())
		return;
	m_cursorVisible = true;
	m_cwlcompositor->handleMouseWheelEvent(ev, m_output);
}

void GlWindow::renderCursor()
{
	// The hwcomposer hardware cursor only covers the built-in screen,
	// so external outputs get a software cursor composited on top of
	// the views.
	QWaylandOutput *primaryOutput = m_cwlcompositor->defaultOutput();
	if (!m_output || !m_cursorVisible || m_output == primaryOutput)
		return;
	if (!m_cursorTexture || !m_cursorTexture->isCreated())
		return;

	// Same draw sequence as the working pointer on the multi-desktop
	// branch: bind the texture target explicitly, draw at full
	// opacity, blit with a top-left origin.
	m_currentTarget = m_cursorTexture->target();
	m_textureBlitter.bind(m_currentTarget);
	m_textureBlitter.setOpacity(1.0);

	const int outputScale = qMax(1, m_output->scaleFactor());
	const QRectF targetRect(
		m_cursorPosition * outputScale,
		QSizeF(m_cursorTexture->width(), m_cursorTexture->height()) *
			outputScale);
	m_textureBlitter.blit(m_cursorTexture->textureId(),
			      QOpenGLTextureBlitter::targetTransform(
				      targetRect, QRect(QPoint(), size())),
			      QOpenGLTextureBlitter::OriginTopLeft);
}