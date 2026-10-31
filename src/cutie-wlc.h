#ifndef CWL_COMPOSITOR_H
#define CWL_COMPOSITOR_H

#include <view.h>
#include <layer-shell.h>
#include <workspace.h>
#include <cutie-shell.h>
#include <QEventPoint>
#include <QHash>
#include <QTimer>
#include <QProcess>
#include <QPropertyAnimation>
#include <QWaylandXdgDecorationManagerV1>

QT_BEGIN_NAMESPACE

class QScreen;
class GlWindow;
class RawPointer;
class CwlWorkspace;
class CutieShell;
class OutputManagerV1;
class OutputPowerManagerV1;
class ScreencopyManagerV1;
class ForeignToplevelManagerV1;
class ForeignToplevelHandleV1;
class InputMethodManagerV2;

class CwlCompositor : public QWaylandCompositor {
	Q_OBJECT
	Q_PROPERTY(double blur READ blur WRITE setBlur NOTIFY blurChanged)
	Q_PROPERTY(double launcherPosition READ launcherPosition WRITE
			   setLauncherPosition NOTIFY launcherPositionChanged)
    public:
	CwlCompositor(GlWindow *glwindow);
	~CwlCompositor() override;
	void create() override;

	QList<CwlView *> getViews() const;
	QList<CwlView *> getToplevelViews();
	CwlView *viewAt(const QPoint &position, QWaylandOutput *output = nullptr);
	CwlView *findView(QWaylandSurface *s);
	CwlView *findTlView(QWaylandSurface *s);
	CwlView *findTreeView(QWaylandSurface *s, CwlView *rootView);

	void raise(CwlView *cwlview);

	void handleTouchEvent(QList<QEventPoint> points,
			      QWaylandOutput *output = nullptr);
	void handleMouseMoveEvent(QList<QEventPoint> points,
				  QWaylandOutput *output = nullptr);
	void handleMousePressEvent(QList<QEventPoint> points, Qt::MouseButton btn,
				   QWaylandOutput *output = nullptr);
	void handleMouseReleaseEvent(QList<QEventPoint> points, Qt::MouseButton btn,
				     QWaylandOutput *output = nullptr);
	void handleMouseMoveEvent(const QPointF &position, QWaylandOutput *output);
	void handleMousePressEvent(const QPointF &position, Qt::MouseButton btn,
				   QWaylandOutput *output);
	void handleMouseReleaseEvent(const QPointF &position, Qt::MouseButton btn,
				     QWaylandOutput *output);
	QWaylandOutput *outputAtGlobal(const QPointF &global, QPointF *local) const;
	QWaylandOutput *outputForToplevel(CwlView *view);
	QRect outputGeometry(QWaylandOutput *output) const;
	QRect outputFullGeometry(QWaylandOutput *output) const;
	QList<QWaylandOutput *> externalOutputs() const;

	bool handleGesture(QPointerEvent *ev, int edge, int corner);

	void handleKeyPress(quint32 nativeScanCode);
	void handleKeyRelease(quint32 nativeScanCode);

	void startRender(QWaylandOutput *output);
	void endRender(QWaylandOutput *output);

	void updateCursor(const QPointF &position, QWaylandOutput *output);

	// While the pointer is on the external screen, the position comes from
	// RawPointer and Qt's own (clamped) mouse positions are ignored.
	bool pointerCaptured() const
	{
		return m_captureOutput != nullptr;
	}

	int scaleFactor();
	void setScaleFactor(int scale);

	CwlView *getTopPanel();
	GlWindow *glWindow();
	ForeignToplevelManagerV1 *foreignTlManagerV1();

	CwlView *m_launcherView = nullptr;
	CwlWorkspace *m_workspace = nullptr;

	// Compositor-drawn pointer. Position is in device pixels, local to
	// m_cursorOutput; only the window of that output draws it.
	CwlView *m_cursorView = nullptr;
	QWaylandOutput *m_cursorOutput = nullptr;
	QPointF m_cursorPos;
	QPoint m_cursorHotspot;
	bool m_cursorHidden = false;
	bool m_mouseActive = false;
	QWaylandOutput *m_captureOutput = nullptr;
	QPointF m_capturePos;
	QPointF m_primaryPos;

	CwlView *getHomeView();

	double blur();
	void setBlur(double blur);

	double launcherPosition();
	void setLauncherPosition(double position);

	void grabSurface(QWaylandSurfaceGrabber *grabber,
			 const QWaylandBufferRef &buffer) override;

    signals:
	void scaleFactorChanged(int scaleFactor);
	void blurChanged(double blur);
	void launcherPositionChanged(double launcherPosition);
	void specialKey(CutieShell::SpecialKey key);

    public slots:
	void triggerRender();
	void onScreenAdded(QScreen *screen);
	void onScreenRemoved(QScreen *screen);
	void onToplevelDamaged(CwlView *view);
	void onHideKeyboard();

    private slots:
	void onXdgToplevelCreated(QWaylandXdgToplevel *toplevel,
				  QWaylandXdgSurface *xdgSurface);
	void onXdgPopupCreated(QWaylandXdgPopup *popup,
			       QWaylandXdgSurface *xdgSurface);
	void onLayerShellSurfaceCreated(LayerSurfaceV1 *layerSurface);
	void viewSurfaceDestroyed();
	void initInputMethod();
	void onRawMotion(int dx, int dy);
	void animationValueChanged(const QVariant &value);
	void createExternalOutput(QScreen *screen, GlWindow *window);
	void routeExistingViews(QWaylandOutput *from, QWaylandOutput *to);

    private:
	CwlView *findView(const QWaylandSurface *s) const;
	GlWindow *m_glwindow = nullptr;
	QWaylandXdgShell *m_xdgShell = nullptr;
	QPointer<CwlView> m_mouseView;
	LayerShellV1 *m_layerShell = nullptr;
	QPointF *m_appPointStart = nullptr;
	CwlView *m_appView = nullptr;
	CutieShell *m_cutieshell = nullptr;
	OutputManagerV1 *m_outputManager = nullptr;
	OutputPowerManagerV1 *m_outputPowerManager = nullptr;
	ScreencopyManagerV1 *m_screencopyManager = nullptr;
	ForeignToplevelManagerV1 *m_foreignTlManagerV1 = nullptr;
	QWaylandOutput *m_output = nullptr;
	QHash<QScreen *, GlWindow *> m_externalWindows;
	QHash<QScreen *, QWaylandOutput *> m_externalOutputs;
	QWaylandXdgDecorationManagerV1 *m_xdgdecoration = nullptr;
	InputMethodManagerV2 *m_inputMngr = nullptr;
	CwlView *m_homeView = nullptr;
	CwlView *m_panelView = nullptr;

	QPropertyAnimation *blurAnim =
		new QPropertyAnimation(this, "blur", this);
	QPropertyAnimation *unblurAnim =
		new QPropertyAnimation(this, "blur", this);

	QPropertyAnimation *launcherCloseAnim =
		new QPropertyAnimation(this, "launcherPosition", this);
	QPropertyAnimation *launcherOpenAnim =
		new QPropertyAnimation(this, "launcherPosition", this);

	int m_scaleFactor = 1;
	bool m_created = false;
	QTimer *m_mouseInactivityTimer = nullptr;
	RawPointer *m_rawPointer = nullptr;
	double m_blur = 0.0;
	double m_launcherPosition = 1.0;
	bool m_homeOpen = true;
	QString launcher = "cutie-launcher";
};

QT_END_NAMESPACE

#endif // CWL_COMPOSITOR_H
