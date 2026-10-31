#pragma once

#include <QtWaylandCompositor/QWaylandCompositor>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QtWaylandCompositor/QWaylandSurfaceGrabber>
#include <QtWaylandCompositor/QWaylandView>
#include <QtWaylandCompositor/QWaylandOutput>
#include <QtWaylandCompositor/QWaylandXdgSurface>
#include <QTimer>
#include <QOpenGLTextureBlitter>

#include <layer-shell.h>

QT_BEGIN_NAMESPACE

class QOpenGLTexture;
class QWaylandXdgShell;
class CwlCompositor;
class TextInputV1;
class TextInputV2;
class TextInputV3;

enum CwlViewLayer : uint32_t {
	BACKGROUND = 0,
	BOTTOM,
	TOP,
	OVERLAY,
	UNDEFINED,
	NUM_LAYERS
};

enum CwlTopPanel : uint32_t {
	PANEL_UNDEFINED = 0,
	PANEL_FOLDED = 1,
	PANEL_UNFOLDING = 2
};

enum CwlViewAnchor : uint32_t {
	ANCHOR_TOP = 1,
	ANCHOR_BOTTOM = 2,
	ANCHOR_LEFT = 4,
	ANCHOR_RIGHT = 8
};

// Window mode for toplevels on external outputs ("the big screen").
// The primary screen keeps its mobile always-maximized behavior.
enum CwlWindowMode : uint32_t {
	WindowNormal = 0,
	WindowMaximized,
	WindowMinimized
};

class CwlView : public QWaylandView {
	Q_OBJECT
    public:
	explicit CwlView(CwlCompositor *cwlcompositor, QRect geometry);
	~CwlView() override;

	QOpenGLTexture *getTexture();
	QOpenGLTextureBlitter::Origin textureOrigin();
	QPointF getPosition();
	void setPosition(const QPointF &pos);
	QSize size();

	CwlViewLayer layer = UNDEFINED;
	CwlTopPanel panelState = PANEL_UNDEFINED;

	bool isToplevel();
	bool isPopup();
	LayerSurfaceV1 *getLayerSurface();
	bool isHidden();
	void setHidden(bool hide);

	CwlWindowMode windowMode() const;
	bool isMinimized() const;
	QRectF windowedGeometry() const;
	void requestMaximize();
	void requestWindowed(const QRectF &geometry);
	void requestMinimize();
	void requestRestore();

	QWaylandXdgToplevel *getTopLevel();
	QWaylandXdgPopup *getPopup();

	QList<CwlView *> getChildViews();
	void removeChildView(CwlView *view);
	void addChildView(CwlView *view);
	CwlView *parentView();
	void setParentView(CwlView *view);
	void setTopLevel(QWaylandXdgToplevel *toplevel);
	void setDesktopOutput(QWaylandOutput *output, const QRect &geometry);
	void setPopUp(QWaylandXdgPopup *popup);

	void setAppId();

	QString getAppId();
	QString getTitle();

	void setLayerSurface(LayerSurfaceV1 *surface);

	TextInputV1 *tiV1 = nullptr;
	TextInputV2 *tiV2 = nullptr;
	TextInputV3 *tiV3 = nullptr;
	uint32_t imContentHint = 0;
	uint32_t imContentPurpose = 0;

	QWaylandSurfaceGrabber *grabber();

    private:
	friend class CwlCompositor;
	CwlCompositor *m_cwlcompositor = nullptr;
	QOpenGLTexture *m_texture = nullptr;
	QOpenGLTextureBlitter::Origin m_origin;
	QPointF m_position;
	QSize m_size;
	QWaylandXdgPopup *m_xdgPopup = nullptr;
	QWaylandXdgToplevel *m_toplevel = nullptr;
	LayerSurfaceV1 *m_layerSurface = nullptr;
	CwlView *m_parentView = nullptr;
	bool m_hidden = false;
	QList<CwlView *> m_childViewList;
	QRect m_availableGeometry;
	bool m_isTopLevel = false;
	bool m_isPopup = false;
	bool m_isLayerShell = false;
	bool m_isImageBuffer = false;
	QString m_cwlAppId = "";
	QWaylandSurfaceGrabber *m_grabber = nullptr;
	CwlWindowMode m_windowMode = CwlWindowMode::WindowMaximized;
	CwlWindowMode m_modeBeforeMinimize = CwlWindowMode::WindowMaximized;
	QRectF m_windowedGeometry;
	void applyWindowMode();

    public slots:
	void onAvailableGeometryChanged(QRect geometry);
	void onRedraw();

    private slots:
	void onAppIdChanged();
	void onWindowGeometryChanged();
	void onLayerSurfaceDataChanged(LayerSurfaceV1 *surface);
	void onDestinationSizeChanged();
	void onSurfaceChanged();
	void onPopUpGeometryChanged();
};

QT_END_NAMESPACE
