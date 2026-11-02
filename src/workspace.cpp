#include <workspace.h>

CwlWorkspace::CwlWorkspace(CwlCompositor *compositor)
	: m_compositor(compositor)
{
	m_outputGeometry = compositor->defaultOutput()->geometry();
	m_outputGeometry =
		QRect(m_outputGeometry.topLeft() / compositor->scaleFactor(),
		      m_outputGeometry.size() / compositor->scaleFactor());
	m_availableGeometry = m_outputGeometry;
}

QRect CwlWorkspace::availableGeometry()
{
	return m_availableGeometry;
}

QRect CwlWorkspace::outputGeometry()
{
	return m_outputGeometry;
}

void CwlWorkspace::removeView(CwlView *view)
{
	m_viewLayerList[view->layer].removeAll(view);
	updateViewList();
	if (view->getLayerSurface() != nullptr &&
	    view->getLayerSurface()->ls_zone > 0)
		updateAvailableGeometry();
	if (view->isToplevel())
		emit toplevelDestroyed(view);
}

void CwlWorkspace::addView(CwlView *view)
{
	m_viewLayerList[view->layer] << view;
	updateViewList();
	if (view->isToplevel() && !view->isHidden())
		showDesktop(false);
}

void CwlWorkspace::raiseView(CwlView *view)
{
	m_viewLayerList[view->layer].removeAll(view);
	m_viewLayerList[view->layer] << view;
	updateViewList();
	if (view->isToplevel() && !view->isHidden())
		showDesktop(false);
}

QList<CwlView *> CwlWorkspace::getViews()
{
	return m_viewList;
}

QList<CwlView *> CwlWorkspace::getToplevelViews()
{
	return m_viewLayerList[CwlViewLayer::TOP];
}

QList<CwlView *> CwlWorkspace::getAllViews() const
{
	QList<CwlView *> views;
	for (int layer = 0; layer < CwlViewLayer::NUM_LAYERS; ++layer)
		views << m_viewLayerList[layer];
	return views;
}

void CwlWorkspace::hideAllTopLevel()
{
	for (CwlView *view : m_viewLayerList[CwlViewLayer::TOP])
		view->setHidden(true);
	showDesktop(true);
}

void CwlWorkspace::showDesktop(bool show)
{
	m_showDesktop = show;
	updateViewList();
}

void CwlWorkspace::singleView(bool single)
{
	m_singleView = single;
	updateViewList();
}

void CwlWorkspace::updateViewList()
{
	m_viewList = m_viewLayerList[CwlViewLayer::BACKGROUND] +
		     m_viewLayerList[CwlViewLayer::BOTTOM];

	if (!m_showDesktop && !m_viewLayerList[CwlViewLayer::TOP].isEmpty())
		if (m_singleView)
			m_viewList << m_viewLayerList[CwlViewLayer::TOP].last();
		else
			m_viewList << m_viewLayerList[CwlViewLayer::TOP];

	m_viewList << m_viewLayerList[CwlViewLayer::OVERLAY];
	updateAvailableGeometry();
}

QRect CwlWorkspace::applyExclusiveZones(QRect geometry, QWaylandOutput *output)
{
	QList<LayerSurfaceV1 *> lsList = getLayerSurfaces(output);
	for (LayerSurfaceV1 *surface : lsList) {
		if (surface && surface->initialized) {
			if (surface->ls_anchor == 1 ||
			    surface->ls_anchor == 13) {
				geometry.setTop(geometry.top() +
						surface->ls_zone);
			} else if (surface->ls_anchor == 2 ||
				   surface->ls_anchor == 14) {
				geometry.setHeight(geometry.height() -
						   surface->ls_zone);
			} else if (surface->ls_anchor == 4 ||
				   surface->ls_anchor == 7) {
				geometry.setLeft(geometry.left() +
						 surface->ls_zone);
			} else if (surface->ls_anchor == 8 ||
				   surface->ls_anchor == 11) {
				geometry.setWidth(geometry.width() -
						  surface->ls_zone);
			}
		}
	}
	return geometry;
}

QRect CwlWorkspace::availableGeometry(QWaylandOutput *output)
{
	if (!output || output == m_compositor->defaultOutput())
		return m_availableGeometry;
	return applyExclusiveZones(m_compositor->outputFullGeometry(output),
				   output);
}

void CwlWorkspace::updateAvailableGeometry()
{
	QRect geometry =
		applyExclusiveZones(m_outputGeometry, m_compositor->defaultOutput());

	if (geometry != m_availableGeometry) {
		m_availableGeometry = geometry;
		emit availableGeometryChanged(m_availableGeometry);
	}

	updateExternalAvailableGeometry();
}

void CwlWorkspace::updateExternalAvailableGeometry()
{
	for (QWaylandOutput *output : m_compositor->externalOutputs()) {
		const QRect geometry = availableGeometry(output);
		for (CwlView *root : m_viewLayerList[CwlViewLayer::TOP]) {
			if (root->output() != output)
				continue;
			QList<CwlView *> pending{ root };
			while (!pending.isEmpty()) {
				CwlView *view = pending.takeLast();
				pending.append(view->getChildViews());
				if (view->isToplevel() &&
				    view->getAvailableGeometry() != geometry)
					view->applyAvailableGeometry(geometry);
			}
		}
	}
}

void CwlWorkspace::onLayerSurfaceDataChanged(LayerSurfaceV1 *surface)
{
	if (!surface->initialized)
		return;
	surface->send_configure(surface->ls_serial,
		surface->size.width(),
		surface->size.height());
	updateAvailableGeometry();
}

QList<LayerSurfaceV1 *> CwlWorkspace::getLayerSurfaces(QWaylandOutput *output)
{
	QList<LayerSurfaceV1 *> ret;
	for (CwlView *view : m_viewList)
		if (view->getLayerSurface() && view->output() == output)
			ret << view->getLayerSurface();
	return ret;
}
