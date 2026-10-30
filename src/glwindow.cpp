#include <glwindow.h>

#include <QPainter>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QDebug>
#include <QStringList>
#include <QMouseEvent>
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
	m_lastExternalViewCount = -1;
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

	QImage img(16, 16, QImage::Format_RGBA8888);
	img.fill(Qt::red);
	m_fallbackCursor = new QOpenGLTexture(img);
	m_fallbackCursor->create();

	emit glReady();
}

void GlWindow::paintGL()
{
	if (m_displayOff || !m_output)
		return;
	m_cwlcompositor->startRender(m_output);
	const int outputScale = m_output->scaleFactor();
	const bool primaryOutput = m_output == m_cwlcompositor->defaultOutput();

	QOpenGLFunctions *functions = context()->functions();
	functions->glClearColor(.0f, .0f, .0f, 1.f);
	functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	m_currentTarget = GL_TEXTURE_2D;
	m_textureBlitter.bind(m_currentTarget);
	functions->glEnable(GL_BLEND);
	functions->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	QList<CwlView *> renderViews;
	if (primaryOutput) {
		renderViews = m_cwlcompositor->getViews();
		if (m_cwlcompositor->m_launcherView &&
		    m_cwlcompositor->m_launcherView->output() == m_output)
			renderViews << m_cwlcompositor->m_launcherView;
	} else {
		renderViews = m_cwlcompositor->getViewsForOutput(m_output);
	}
	if (!primaryOutput && renderViews.size() != m_lastExternalViewCount) {
		QStringList summary;
		for (CwlView *view : renderViews) {
			LayerSurfaceV1 *layerSurface = view->getLayerSurface();
			const QString name = layerSurface ?
				layerSurface->ls_scope :
				(view->isToplevel() ? view->getAppId() :
						      QStringLiteral("surface"));
			summary << QStringLiteral(
				"%1(layer=%2,size=%3x%4,content=%5,pos=%6,%7)")
					.arg(name)
					.arg(static_cast<int>(view->layer))
					.arg(view->size().width())
					.arg(view->size().height())
					.arg(view->surface() &&
					     view->surface()->hasContent())
					.arg(view->getPosition().x())
					.arg(view->getPosition().y());
		}
		qInfo().noquote() << "Cutie: external output render stack"
				  << m_output->model()
				  << summary.join(QStringLiteral(", "));
		m_lastExternalViewCount = renderViews.size();
	}

	for (CwlView *view : renderViews) {
		if (view->output() != m_output)
			continue;
		QString appId;
		if (view && view->isToplevel())
			appId = view->getAppId();

		if (!primaryOutput)
			m_textureBlitter.setOpacity(1.0);
		else if (appId == "cutie-launcher")
			m_textureBlitter.setOpacity(
				1.0 -
				(m_cwlcompositor->m_launcherView->getPosition()
					 .y() - m_output->position().y()) *
				 outputScale / height());
		else if (view->isToplevel())
			if (m_cwlcompositor->launcherPosition() > 0.0 &&
			    m_cwlcompositor->m_launcherView)
				m_textureBlitter.setOpacity(
					m_cwlcompositor->blur() *
					(m_cwlcompositor->m_launcherView
						 ->getPosition()
						 .y() - m_output->position().y()) *
					outputScale / height());
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

	// Render mouse cursor only when actively in use
	if (primaryOutput && m_cwlcompositor->m_mouseActive) {
		if (m_cwlcompositor->m_cursorView && m_cwlcompositor->m_cursorView->getTexture()) {
			m_cwlcompositor->m_cursorView->setPosition(m_cwlcompositor->m_cursorPos - m_cwlcompositor->m_cursorHotspot);
			m_cwlcompositor->m_cursorView->setOutput(m_output);
			m_textureBlitter.setOpacity(1.0);
			renderView(m_cwlcompositor->m_cursorView);
		} else if (m_fallbackCursor) {
			QRectF targetRect(
				m_cwlcompositor->m_cursorPos.x(),
				m_cwlcompositor->m_cursorPos.y(),
				24, 24
			);
			QMatrix4x4 targetTransform = QOpenGLTextureBlitter::targetTransform(
				targetRect, QRect(QPoint(), size())
			);
			m_textureBlitter.bind(m_fallbackCursor->target());
			m_textureBlitter.setOpacity(1.0);
			m_textureBlitter.blit(m_fallbackCursor->textureId(), targetTransform,
					      QOpenGLTextureBlitter::OriginTopLeft);
		}
	}

	m_textureBlitter.release();
	m_cwlcompositor->endRender(m_output);
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
		QPointF viewPosition = view->getPosition();
		viewPosition -= m_output->position();
		auto surfaceOrigin = view->textureOrigin();
		const int outputScale = m_output->scaleFactor();
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
		m_cwlcompositor->handleMouseMoveEvent(ev->points(), m_output);
		return;
	}
	// Directly update cursor position and trigger render on any mouse move event
	m_cwlcompositor->handleMouseMoveEvent(ev->points(), m_output);
	m_cwlcompositor->triggerRender();
}

void GlWindow::mousePressEvent(QMouseEvent *ev)
{
	Qt::MouseButton btn = ev->button();
	if (m_output && m_output != m_cwlcompositor->defaultOutput()) {
		m_cwlcompositor->handleMousePressEvent(ev->points(), btn, m_output);
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