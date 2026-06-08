#pragma once

#include <QOpenGLWindow>
#include <QOpenGLTextureBlitter>
#include <QEventPoint>
#include <QResizeEvent> // Added for resizeEvent support
#include <QTimer>
#include <atomic>

#include <cutie-wlc.h>
#include <gesture.h>

QT_BEGIN_NAMESPACE

class GlWindow : public QOpenGLWindow {
    Q_OBJECT
    public:
    GlWindow();
    void setCompositor(CwlCompositor *cwlcompositor);
    bool displayOff();
    void setDisplayOff(bool displayOff);
    void scheduleUpdate();
    void startBoost();
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

    // --- MINIMAL FIX: Added declaration ---
    void resizeEvent(QResizeEvent *ev) override;
    // --------------------------------------

    void touchEvent(QTouchEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    private:
    void renderView(CwlView *view);

    QOpenGLTextureBlitter m_textureBlitter;
    GLenum m_currentTarget;
    QOpenGLTexture *m_wallpaper = nullptr;

    QList<QEventPoint *> m_evPoint;
    bool m_displayOff = false;

    // Atomic so it can be safely written from the main thread
    // and read/cleared from the render thread (threaded render loop)
    std::atomic<bool> m_pendingUpdate{false};

    // Render boost: keeps frame callbacks firing at 60hz for a short
    // window after the last touch event, giving fling animations a
    // regular cadence without burning GPU when the screen is idle.
    bool m_boostActive = false;
    QTimer *m_frameTimer = nullptr;   // 16ms heartbeat, runs while boost active
    QTimer *m_boostTimeout = nullptr; // turns boost off after inactivity

    CwlCompositor *m_cwlcompositor = nullptr;
    CwlGesture *m_gesture = nullptr;
};

QT_END_NAMESPACE