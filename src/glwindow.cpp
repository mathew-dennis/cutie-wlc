#include <glwindow.h>

#include <QPainter>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformscreen.h>
#include <QtWaylandCompositor/QWaylandSeat>
#include <QtWaylandCompositor/QWaylandOutputMode>

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

    // Safety: ensure we don't pass 0,0 to the gesture handler
    int w = width() > 0 ? width() : 720;
    int h = height() > 0 ? height() : 1280;
    m_gesture = new CwlGesture(cwlcompositor, QSize(w, h));
}

// --- Handle window resizing ---
void GlWindow::resizeEvent(QResizeEvent *ev)
{
    QOpenGLWindow::resizeEvent(ev);
    if (m_cwlcompositor && m_cwlcompositor->defaultOutput()) {
        QSize newSize = ev->size();
        if (newSize.width() > 0 && newSize.height() > 0) {
            QWaylandOutputMode mode(newSize, 60000);
            m_cwlcompositor->defaultOutput()->addMode(mode, true);
            m_cwlcompositor->defaultOutput()->setCurrentMode(mode);

            // Re-sync gesture area to new size
            if (m_gesture) {
                delete m_gesture;
                m_gesture = new CwlGesture(m_cwlcompositor, newSize);
            }
        }
    }
}

void GlWindow::scheduleUpdate()
{
    // Only call requestUpdate() if there isn't already one pending.
    // m_pendingUpdate is atomic so this is safe across the main thread
    // (writers) and the render thread (reader/clearer in paintGL).
    if (!m_pendingUpdate.exchange(true)) {
        requestUpdate();
    }
}

void GlWindow::startBoost()
{
    // Defensive guard: timers are created in initializeGL() which runs
    // after the GL context is ready. Touch events can theoretically arrive
    // before that in rare race conditions.
    if (!m_boostTimeout || !m_frameTimer)
        return;

    // Activate the boost flag so the timer lambda starts calling endRender().
    m_boostActive = true;

    // Restart the inactivity timeout — extends the window on every touch
    // so continuous scrolling stays boosted without any gap.
    m_boostTimeout->start();

    // FIX: restart m_frameTimer if it was stopped during the previous idle
    // period. Without this, after the first boost window expires and the
    // timer is stopped, no subsequent touch event would ever get a boost.
    if (!m_frameTimer->isActive())
        m_frameTimer->start();
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
        if (m_cwlcompositor) {
            m_cwlcompositor->setLauncherPosition(0.0);
            m_cwlcompositor->onHideKeyboard();
        }
    } else
        scheduleUpdate();

    m_displayOff = displayOff;
    emit displayOffChanged(m_displayOff);
}

void GlWindow::initializeGL()
{
    m_textureBlitter.create();

    // --- Render boost heartbeat timer ---
    // Sends frame callbacks to Wayland clients at ~60hz while a boost
    // window is active. The timer is stopped during idle so it consumes
    // no resources when the screen is static. startBoost() restarts it
    // on the next touch event.
    //
    // FIX: the lambda calls endRender() — NOT scheduleUpdate(). These do
    // different things:
    //   scheduleUpdate() → triggers a compositor repaint (GPU work)
    //   endRender()      → sends wl_frame callbacks to clients so they
    //                       can submit their next animation frame
    // The fling stutter fix requires the latter — clients need a regular
    // callback cadence to drive their kinetic scroll deceleration.
    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(16); // ~60hz; reduce to 8 for 120hz displays
    m_frameTimer->setTimerType(Qt::PreciseTimer);
    connect(m_frameTimer, &QTimer::timeout, this, [this]() {
        if (!m_displayOff && m_cwlcompositor && m_boostActive)
            m_cwlcompositor->endRender();
    });
    // Start stopped — only runs during an active boost window.
    // startBoost() will start it on first touch.

    // --- Boost inactivity timeout ---
    // Deactivates the boost and stops the frame timer 1.2 seconds after
    // the last user interaction. This covers virtually all fling decay
    // animations. Single-shot so it only fires once per touch sequence;
    // startBoost() restarts it on each new touch to extend the window.
    // Tune: 0.8s saves more power, 1.5s covers very slow flings.
    m_boostTimeout = new QTimer(this);
    m_boostTimeout->setInterval(1200);
    m_boostTimeout->setSingleShot(true);
    m_boostTimeout->setTimerType(Qt::PreciseTimer);
    connect(m_boostTimeout, &QTimer::timeout, this, [this]() {
        m_boostActive = false;
        // Stop the heartbeat timer during idle — zero wakeups, zero
        // client callbacks, minimal power draw until next touch.
        if (m_frameTimer)
            m_frameTimer->stop();
    });

    emit glReady();
}

void GlWindow::paintGL()
{
    // Clear the flag first so any damage that arrives while we are
    // painting will schedule a fresh update rather than being dropped.
    m_pendingUpdate.store(false);

    if (m_displayOff || !m_cwlcompositor)
        return;
    m_cwlcompositor->startRender();

    QOpenGLFunctions *functions = context()->functions();
    functions->glClearColor(.0f, .0f, .0f, 1.f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_currentTarget = GL_TEXTURE_2D;
    m_textureBlitter.bind(m_currentTarget);
    functions->glEnable(GL_BLEND);
    functions->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    QList<CwlView *> renderViews;
    if (m_cwlcompositor->m_launcherView)
        renderViews = m_cwlcompositor->getViews()
                      << m_cwlcompositor->m_launcherView;
    else
        renderViews = m_cwlcompositor->getViews();

    for (CwlView *view : renderViews) {
        if (!view) continue; // Safety check
        QString appId;
        if (view->isToplevel())
            appId = view->getAppId();

        int winHeight = height() > 0 ? height() : 1280;

        if (appId == "cutie-launcher")
            m_textureBlitter.setOpacity(
                1.0 -
                (m_cwlcompositor->m_launcherView->getPosition()
                     .y() *
                 m_cwlcompositor->scaleFactor() / winHeight));
        else if (view->isToplevel())
            if (m_cwlcompositor->launcherPosition() > 0.0)
                m_textureBlitter.setOpacity(
                    m_cwlcompositor->blur() *
                    m_cwlcompositor->m_launcherView
                        ->getPosition()
                        .y() *
                    m_cwlcompositor->scaleFactor() /
                    winHeight);
            else
                m_textureBlitter.setOpacity(
                    m_cwlcompositor->blur());
        else
            m_textureBlitter.setOpacity(1.0);

        if (m_cwlcompositor->launcherPosition() == 1.0 &&
            view->layer == CwlViewLayer::TOP)
            continue;

        renderView(view);
    }

    m_textureBlitter.release();

    // endRender() / sendFrameCallbacks() is intentionally NOT called here.
    // It is owned exclusively by m_frameTimer during active boost windows.
    // This gives clients a steady 60hz callback cadence for fling animations
    // while keeping idle screens completely quiet.
}

void GlWindow::renderView(CwlView *view)
{
    if (!view) return;
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
        auto surfaceOrigin = view->textureOrigin();
        int scale = m_cwlcompositor->scaleFactor();
        QRectF targetRect(viewPosition * scale, viewSize * scale);

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
    if (!m_gesture || !m_cwlcompositor) return;
    startBoost();
    m_gesture->handlePointerEvent(ev, [this](QList<QEventPoint> points) {
        m_cwlcompositor->handleTouchEvent(points);
    });
}

void GlWindow::mouseMoveEvent(QMouseEvent *ev)
{
    if (!m_gesture || !m_cwlcompositor) return;
    startBoost();
    m_gesture->handlePointerEvent(ev, [this](QList<QEventPoint> points) {
        m_cwlcompositor->handleMouseMoveEvent(points);
    });
}

void GlWindow::mousePressEvent(QMouseEvent *ev)
{
    if (!m_gesture || !m_cwlcompositor) return;
    startBoost();
    Qt::MouseButton btn = ev->button();
    m_gesture->handlePointerEvent(ev, [this,
                                       btn](QList<QEventPoint> points) {
        m_cwlcompositor->handleMousePressEvent(points, btn);
    });
}

void GlWindow::mouseReleaseEvent(QMouseEvent *ev)
{
    if (!m_gesture || !m_cwlcompositor) return;
    startBoost();
    Qt::MouseButton btn = ev->button();
    m_gesture->handlePointerEvent(ev, [this,
                                       btn](QList<QEventPoint> points) {
        m_cwlcompositor->handleMouseReleaseEvent(points, btn);
    });
}

void GlWindow::keyPressEvent(QKeyEvent *event)
{
    if (!m_cwlcompositor) return;
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
    if (!m_cwlcompositor) return;
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
