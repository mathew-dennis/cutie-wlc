#pragma once

#include <cutie-wlc.h>

class CwlWorkspace : public QObject {
	Q_OBJECT
    public:
	CwlWorkspace(CwlCompositor *compositor);

	QRect availableGeometry();
	QRect availableGeometry(QWaylandOutput *output);
	QRect outputGeometry();

	void removeView(CwlView *view);
	void addView(CwlView *view);
	void raiseView(CwlView *view);
	QList<CwlView *> getViews();
	QList<CwlView *> getToplevelViews();
	QList<CwlView *> getAllViews() const;

	void hideAllTopLevel();
	void showDesktop(bool show);
	void singleView(bool single);

    public slots:
	void onLayerSurfaceDataChanged(LayerSurfaceV1 *surface);

    signals:
	void availableGeometryChanged(QRect geometry);
	void toplevelCreated(CwlView *view);
	void toplevelDestroyed(CwlView *view);

    private:
	CwlCompositor *m_compositor = nullptr;
	QRect m_availableGeometry;
	QRect m_outputGeometry;
	bool m_showDesktop = false;
	bool m_singleView = true;

	QList<CwlView *> m_viewList;
	QList<CwlView *> m_viewLayerList[CwlViewLayer::NUM_LAYERS];

	QList<LayerSurfaceV1 *> getLayerSurfaces(QWaylandOutput *output);

	void updateViewList();
	QRect applyExclusiveZones(QRect geometry, QWaylandOutput *output);
	void updateAvailableGeometry();
	void updateExternalAvailableGeometry();
};
