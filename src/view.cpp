#include <view.h>
#include <decoration.h>
#include <cutie-wlc.h>
#include <QOpenGLTexture>
#include <QtWaylandCompositor/QWaylandSeat>

CwlView::CwlView(CwlCompositor *cwlcompositor, QRect geometry)
	: m_cwlcompositor(cwlcompositor)
	, m_availableGeometry(geometry)
{
	connect(this, &CwlView::surfaceChanged, this,
		&CwlView::onSurfaceChanged);
}

CwlView::~CwlView()
{
	if (m_isImageBuffer)
		delete m_texture;
	if (m_grabber)
		delete m_grabber;
}

QOpenGLTexture *CwlView::getTexture()
{
	if (advance()) {
		QWaylandBufferRef bufRef = currentBuffer();
		if (bufRef.origin() == QWaylandSurface::OriginTopLeft)
			m_origin = QOpenGLTextureBlitter::OriginTopLeft;
		else
			m_origin = QOpenGLTextureBlitter::OriginBottomLeft;

		if (bufRef.bufferType() ==
		    QWaylandBufferRef::BufferType::BufferType_Egl) {
			m_texture = bufRef.toOpenGLTexture();
		} else if (bufRef.bufferType() ==
			   QWaylandBufferRef::BufferType::
				   BufferType_SharedMemory) {
			m_isImageBuffer = true;
			delete m_texture;
			m_texture = new QOpenGLTexture(bufRef.image());
		}
	}
	return m_texture;
}

QOpenGLTextureBlitter::Origin CwlView::textureOrigin()
{
	return m_origin;
}

QPointF CwlView::getPosition()
{
	return m_position;
}

void CwlView::setPosition(const QPointF &pos)
{
	m_position = pos;
	m_cwlcompositor->triggerRender();
}

QSize CwlView::size()
{
	return surface() ? surface()->destinationSize() : m_size;
}

bool CwlView::isToplevel()
{
	return m_isTopLevel;
}

bool CwlView::isPopup()
{
	return m_isPopup;
}

QWaylandXdgToplevel *CwlView::getTopLevel()
{
	return m_toplevel;
}

QWaylandXdgPopup *CwlView::getPopup()
{
	return m_xdgPopup;
}

LayerSurfaceV1 *CwlView::getLayerSurface()
{
	return m_layerSurface;
}

bool CwlView::isHidden()
{
	return m_hidden;
}

void CwlView::setHidden(bool hide)
{
	m_hidden = hide;
}

void CwlView::onAvailableGeometryChanged(QRect geometry)
{
	if (output() != m_cwlcompositor->defaultOutput())
		return;
	applyAvailableGeometry(geometry);
}

void CwlView::applyAvailableGeometry(const QRect &geometry)
{
	m_availableGeometry = geometry;
	if (layer == TOP) {
		if (isToplevel()) {
			applyWindowMode();
		} else if (m_toplevel->xdgSurface()->popup()) {
			setPosition(m_toplevel->xdgSurface()
					    ->popup()
					    ->unconstrainedPosition() +
				    geometry.topLeft());
		}
	}
}

QList<CwlView *> CwlView::getChildViews()
{
	return m_childViewList;
}

void CwlView::removeChildView(CwlView *view)
{
	m_childViewList.removeAll(view);
	emit m_cwlcompositor->m_workspace->toplevelDestroyed(view);
}

void CwlView::addChildView(CwlView *view)
{
	m_childViewList << view;
}

CwlView *CwlView::parentView()
{
	return m_parentView;
}

void CwlView::setParentView(CwlView *view)
{
	m_parentView = view;
}

void CwlView::setTopLevel(QWaylandXdgToplevel *toplevel)
{
	m_toplevel = toplevel;
	m_isTopLevel = true;
	connect(m_toplevel, &QWaylandXdgToplevel::appIdChanged, this,
		&CwlView::onAppIdChanged);
}

void CwlView::setDesktopOutput(QWaylandOutput *output, const QRect &geometry)
{
	if (!output)
		return;
	QWaylandView::setOutput(output);
	m_availableGeometry = geometry;
	if (m_isTopLevel && m_toplevel) {
		// Windows on the phone are always maximized and visible
		if (output == m_cwlcompositor->defaultOutput()) {
			m_windowed = false;
			m_minimized = false;
		}
		applyWindowMode();
	}
}

bool CwlView::isWindowedOutput()
{
	return output() && output() != m_cwlcompositor->defaultOutput();
}

// Plain toplevels on an external screen can be windowed, maximized, minimized
bool CwlView::isWindowable()
{
	return m_isTopLevel && m_toplevel && !m_parentView && layer == TOP &&
	       isWindowedOutput();
}

// Only windows that let us draw their decoration get a title bar
bool CwlView::hasTitleBar()
{
	return isWindowable() &&
	       m_toplevel->decorationMode() ==
		       QWaylandXdgToplevel::ServerSideDecoration;
}

// Send the size this window should have and put it in place
void CwlView::applyWindowMode()
{
	if (!m_isTopLevel || !m_toplevel)
		return;
	if (m_parentView && isWindowedOutput())
		return;
	if (m_windowed && !m_parentView && isWindowedOutput() &&
	    m_restoreRect.isValid()) {
		m_toplevel->sendUnmaximized(m_restoreRect.size().toSize());
		setPosition(m_restoreRect.topLeft());
		return;
	}
	QRect area = m_availableGeometry;
	if (hasTitleBar())
		area.adjust(0, CwlDecoration::TitleBarHeight, 0, 0);
	m_toplevel->sendMaximized(area.size());
	setPosition(area.topLeft());
}

void CwlView::setWindowMaximized(bool maximize)
{
	if (!isWindowable() || maximize == !m_windowed)
		return;
	if (maximize) {
		const QRectF current(getPosition(), QSizeF(size()));
		if (current.isValid())
			m_restoreRect = current;
		m_windowed = false;
	} else {
		m_windowed = true;
	}
	qInfo() << "Cutie: window" << getAppId()
		<< (maximize ? "maximized" : "restored");
	applyWindowMode();
	m_cwlcompositor->triggerRender();
}

void CwlView::setWindowMinimized(bool minimize)
{
	if (!isWindowable() || m_minimized == minimize)
		return;
	m_minimized = minimize;
	qInfo() << "Cutie: window" << getAppId()
		<< (minimize ? "minimized" : "restored from minimized");
	m_cwlcompositor->onWindowMinimizedChanged(this);
}

void CwlView::setWindowRect(const QRectF &rect, bool resizing)
{
	m_windowed = true;
	m_restoreRect = rect;
	if (resizing) {
		m_toplevel->sendResizing(rect.size().toSize());
		setPosition(rect.topLeft());
	} else {
		applyWindowMode();
	}
}

void CwlView::moveWindow(const QPointF &topLeft)
{
	if (m_windowed)
		m_restoreRect.moveTopLeft(topLeft);
	setPosition(topLeft);
}

void CwlView::setPopUp(QWaylandXdgPopup *popup)
{
	m_xdgPopup = popup;
	m_isPopup = true;
}

void CwlView::setAppId()
{
	if (m_isTopLevel)
		m_cwlAppId = m_toplevel->appId();
	else if (m_isLayerShell)
		m_cwlAppId = m_layerSurface->ls_scope;
}

QString CwlView::getAppId()
{
	return m_cwlAppId;
}

QString CwlView::getTitle()
{
	if (!m_isTopLevel)
		return "";
	return m_toplevel->title();
}

void CwlView::setLayerSurface(LayerSurfaceV1 *surface)
{
	if (!surface)
		return;
	m_layerSurface = surface;
	m_isLayerShell = true;
	setAppId();
	connect(m_layerSurface, &LayerSurfaceV1::layerSurfaceDataChanged, this,
		&CwlView::onLayerSurfaceDataChanged);
	connect(this->surface(), &QWaylandSurface::destinationSizeChanged, this,
		&CwlView::onDestinationSizeChanged);
}

void CwlView::onAppIdChanged()
{
	if (m_toplevel->appId() == getAppId())
		return;
	setAppId();
	QWaylandOutput *targetOutput = m_cwlcompositor->outputForToplevel(this);
	if (targetOutput != output()) {
		setOutput(targetOutput);
		m_availableGeometry =
			m_cwlcompositor->outputGeometry(targetOutput);
	}
	if (getAppId() == "cutie-launcher") {
		layer = OVERLAY;
		m_cwlcompositor->m_launcherView = this;
		m_toplevel->sendMaximized(m_availableGeometry.size());
		m_cwlcompositor->setLauncherPosition(0.0);
	} else {
		layer = TOP;
		if (m_toplevel->parentToplevel()) {
			CwlView *parent_view = m_cwlcompositor->findTlView(
				m_toplevel->parentToplevel()
					->xdgSurface()
					->surface());
			parent_view->addChildView(this);
			this->setParentView(parent_view);
			if (isWindowedOutput()) {
				// Dialogs pick their own size on windowed screens
				m_toplevel->sendUnmaximized(QSize(0, 0));
				this->setPosition(parent_view->getPosition());
			} else {
				m_toplevel->sendMaximized(
					m_availableGeometry.size());
				this->setPosition(m_availableGeometry.topLeft());
			}
			connect(m_toplevel->xdgSurface(),
				&QWaylandXdgSurface::windowGeometryChanged,
				this, &CwlView::onWindowGeometryChanged);
			m_cwlcompositor->raise(parent_view);
			emit m_cwlcompositor->m_workspace->toplevelCreated(
				this);
			m_cwlcompositor->defaultSeat()->setKeyboardFocus(
				this->surface());
		} else {
			if (isWindowedOutput()) {
				m_windowed = true;
				m_restoreRect = m_cwlcompositor->defaultWindowRect(
					output());
				qInfo() << "Cutie: window" << getAppId()
					<< "opens windowed at" << m_restoreRect;
			}
			m_cwlcompositor->m_workspace->addView(this);
			emit m_cwlcompositor->m_workspace->toplevelCreated(
				this);
			applyWindowMode();
			m_cwlcompositor->raise(this);
		}
	}
}

void CwlView::onWindowGeometryChanged()
{
	if (parentView()) {
		QRect parentGeometry = parentView()
					       ->getTopLevel()
					       ->xdgSurface()
					       ->windowGeometry();
		QRect currentGeometry =
			m_toplevel->xdgSurface()->windowGeometry();
		if (isWindowedOutput()) {
			const QRectF parentRect(parentView()->getPosition(),
						QSizeF(parentView()->size()));
			currentGeometry.moveCenter(parentRect.center().toPoint());
			currentGeometry.moveLeft(qBound(
				m_availableGeometry.left(), currentGeometry.left(),
				m_availableGeometry.right() -
					currentGeometry.width() + 1));
			currentGeometry.moveTop(qBound(
				m_availableGeometry.top(), currentGeometry.top(),
				m_availableGeometry.bottom() -
					currentGeometry.height() + 1));
			this->setPosition(currentGeometry.topLeft());
			return;
		}
		if (currentGeometry.size().height() <=
			    m_availableGeometry.size().height() ||
		    currentGeometry.size().width() <=
			    m_availableGeometry.size().width()) {
			currentGeometry.moveCenter(
				m_availableGeometry.center());
		}
		this->setPosition(currentGeometry.topLeft());
	}
}

void CwlView::onLayerSurfaceDataChanged(LayerSurfaceV1 *surface)
{
	if (surface != m_layerSurface)
		return;
	const bool isPrimary = output() == m_cwlcompositor->defaultOutput();
	const QRect screenGeometry = isPrimary
		? m_cwlcompositor->m_workspace->outputGeometry()
		: m_cwlcompositor->outputFullGeometry(output());
	if (m_layerSurface->ls_scope == "cutie-panel" &&
	    m_layerSurface->size.height() <= m_layerSurface->ls_zone)
		panelState = PANEL_FOLDED;

	QPointF position(m_availableGeometry.center().x() -
				 m_layerSurface->size.width() / 2,
			 m_availableGeometry.center().y() -
				 m_layerSurface->size.height() / 2);

	if (CwlViewAnchor::ANCHOR_TOP & m_layerSurface->ls_anchor &&
	    !(CwlViewAnchor::ANCHOR_BOTTOM & m_layerSurface->ls_anchor))
		position.setY(m_layerSurface->ls_zone < 0 ?
				      screenGeometry.top() :
				      m_availableGeometry.top());
	else if (CwlViewAnchor::ANCHOR_BOTTOM & m_layerSurface->ls_anchor &&
		 !(CwlViewAnchor::ANCHOR_TOP & m_layerSurface->ls_anchor))
		position.setY(
			(m_layerSurface->ls_zone < 0 ? screenGeometry.bottom() :
				 m_availableGeometry.bottom()) -
			m_layerSurface->ls_zone);
	else if (m_layerSurface->ls_zone < 0)
		position.setY(screenGeometry.center().y() -
			      m_layerSurface->size.height() / 2);

	if (CwlViewAnchor::ANCHOR_LEFT & m_layerSurface->ls_anchor &&
	    !(CwlViewAnchor::ANCHOR_RIGHT & m_layerSurface->ls_anchor))
		position.setX(m_layerSurface->ls_zone < 0 ?
				      screenGeometry.left() :
				      m_availableGeometry.left());
	else if (CwlViewAnchor::ANCHOR_RIGHT & m_layerSurface->ls_anchor &&
		 !(CwlViewAnchor::ANCHOR_LEFT & m_layerSurface->ls_anchor))
		position.setX(
			(m_layerSurface->ls_zone < 0 ? screenGeometry.right() :
				 m_availableGeometry.right()) -
			m_layerSurface->size.width());
	else if (m_layerSurface->ls_zone < 0)
		position.setX(screenGeometry.center().x() -
			      m_layerSurface->size.width() / 2);

	this->setPosition(position);
}

void CwlView::onDestinationSizeChanged()
{
	if (m_layerSurface && m_layerSurface->ls_scope == "cutie-panel")
		if (this->surface()->destinationSize().height() <=
		    m_layerSurface->ls_zone)
			panelState = PANEL_FOLDED;
		else if (this->surface()->destinationSize().height() >
			 m_layerSurface->ls_zone)
			panelState = PANEL_UNFOLDING;
}

void CwlView::onRedraw()
{
	m_cwlcompositor->triggerRender();
	if (isToplevel())
		;
	m_cwlcompositor->onToplevelDamaged(this);
}

QWaylandSurfaceGrabber *CwlView::grabber()
{
	return m_grabber;
}

void CwlView::onSurfaceChanged()
{
	if (m_grabber)
		delete m_grabber;
	m_grabber = new QWaylandSurfaceGrabber(surface());
}

void CwlView::onPopUpGeometryChanged()
{
	if (!m_availableGeometry.contains(m_xdgPopup->configuredGeometry())) {
		QRect intersected =
			m_xdgPopup->configuredGeometry().intersected(
				m_availableGeometry);
		if (intersected.topLeft() ==
		    m_xdgPopup->configuredGeometry().topLeft()) {
			QRect newRect = m_xdgPopup->configuredGeometry();
			newRect.translate(
				intersected.size().width() -
					m_xdgPopup->configuredGeometry()
						.size()
						.width(),
				intersected.size().height() -
					m_xdgPopup->configuredGeometry()
						.size()
						.height());
			this->setPosition(newRect.topLeft());
		}
	}
}