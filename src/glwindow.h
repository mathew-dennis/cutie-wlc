#pragma once

#include <QOpenGLWindow>
#include <QOpenGLTextureBlitter>
#include <QEventPoint>

#include <cutie-wlc.h>
#include <gesture.h>

QT_BEGIN_NAMESPACE

class GlWindow : public QOpenGLWindow {
	Q_OBJECT
    public:
	GlWindow();
	void setCompositor(CwlCompositor *cwlcompositor);
	void setOutput(QWaylandOutput *output);
	QWaylandOutput *output() const;
	bool displayOff();
	void setDisplayOff(bool displayOff);
	inline CwlGesture *gesture()
	{
		return m_gesture;
	}

    signals:
	void glReady();
	void displayOffChanged(bool displayOff);

    protected:
	void initializeGL() override;
	void paintGL() override;

	void touchEvent(QTouchEvent *ev) override;
	void mouseMoveEvent(QMouseEvent *ev) override;
	void mousePressEvent(QMouseEvent *ev) override;
	void mouseReleaseEvent(QMouseEvent *ev) override;
	void keyPressEvent(QKeyEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;

    private:
	void renderView(CwlView *view);
	void renderCursor();
	void renderTitleBar(CwlView *view);
	void renderBorder(CwlView *view, bool active);
	bool ownsPointer(QWaylandOutput *output) const;
	QWaylandOutput *pointerTarget(const QMouseEvent *ev, QPointF *local) const;
	void trackCursor(const QMouseEvent *ev, const QPointF &local,
			 QWaylandOutput *output);

	QOpenGLTextureBlitter m_textureBlitter;
	QWaylandOutput *m_output = nullptr;
	GLenum m_currentTarget;
	QOpenGLTexture *m_wallpaper = nullptr;
	QOpenGLTexture *m_fallbackCursor = nullptr;
	QOpenGLTexture *m_shapeCursors[6] = {};
	QOpenGLTexture *m_borderTextures[2] = {};

	struct TitleBarTexture {
		QOpenGLTexture *texture = nullptr;
		QSize size;
		QString title;
		bool active = false;
		bool maximized = false;
		int hover = 0;
	};
	QHash<CwlView *, TitleBarTexture> m_titleBars;
	QList<QOpenGLTexture *> m_staleTextures;

	QList<QEventPoint *> m_evPoint;
	bool m_displayOff = false;
    bool m_externalRenderLogged = false;
	
	CwlCompositor *m_cwlcompositor = nullptr;
	CwlGesture *m_gesture = nullptr;
};

QT_END_NAMESPACE
