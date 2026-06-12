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

    int w = width() > 0 ? width() : 720;
    int h = height() > 0 ? height() : 1280;
    m_gesture = new CwlGesture(cwlcompositor, QSize(w, h));
}

int GlWindow::getScreenRefreshRate() const
{
    if (m_cwlcompositor && m_cwlcompositor->defaultOutput()) {
        QWaylandOutputMode currentMode = m_cwlcompositor->defaultOutput()->currentMode();
        if (currentMode.isValid()) {
            int hz = currentMode.refreshRate() / 1000;
            return hz > 0 ? hz : 60;
        }
    }
    return 60;
}

void GlWindow::updateTimerInterval()
{
    if (!m_frameTimer) return;
    
    int hz = getScreenRefreshRate();
    int intervalMs = 1000 / hz; 
    m_frameTimer->setInterval(intervalMs);
}

void GlWindow::resizeEvent(QResizeEvent *ev)
{
    QOpenGLWindow::resizeEvent(ev);
    if (m_cwlcompositor && m_cwlcompositor->defaultOutput()) {
        QSize newSize = ev->size();
        if (newSize.width() > 0 && newSize.height() > 0) {
            // FIX: Preserve the screen's actual refresh rate instead of forcing 60Hz
            int currentMilliHz = getScreenRefreshRate() * 1000;
            QWaylandOutputMode mode(newSize, currentMilliHz); 
            
            m_cwlcompositor->defaultOutput()->addMode(mode, true);
            m_cwlcompositor->defaultOutput()->setCurrentMode(mode);

            updateTimerInterval();

            if (m_gesture) {
                delete m_gesture;
                m_gesture = new CwlGesture(m_cwlcompositor, newSize);
            }
        }
    }
}

void GlWindow::scheduleUpdate()
{
    if (!m_pendingUpdate.exchange(true)) {
        requestUpdate();
    }
}

void GlWindow::startBoost()
{
    if (!m_boostTimeout || !m_frameTimer)
        return;

    m_boostActive = true;
    updateTimerInterval();
    m_boostTimeout->start();

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
    } else {
        scheduleUpdate();
    }

    m_displayOff = displayOff;
    emit displayOffChanged(m_displayOff);
}

void GlWindow::initializeGL()
{
    m_textureBlitter.create();

    m_frameTimer = new QTimer(this);
    m_frameTimer->setTimerType(Qt::PreciseTimer);
    updateTimerInterval();
    
    connect(m_frameTimer, &QTimer::timeout, this, [this]() {
        if (!m_displayOff && m_cwlcompositor && m_boostActive) {
            m_cwlcompositor->endRender(); 
            scheduleUpdate();             
        }
    });

    m_boostTimeout = new QTimer(this);
    m_boostTimeout->setInterval(1200);
    m_boostTimeout->setSingleShot(true);
    m_boostTimeout->setTimerType(Qt::PreciseTimer);
    
    connect(m_boostTimeout, &QTimer::timeout, this, [this]() {
        m_boostActive = false;
        m_frameTimer->stop();
        
        if (!m_displayOff && m_cwlcompositor) {
            m_cwlcompositor->endRender();
            requestUpdate(); 
        }
    });

    emit glReady();
}

void GlWindow::paintGL()
{
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

    QList<CwlView *> renderViews = m_cwlcompositor->getViews();
    if (m_cwlcompositor->m_launcherView) {
        renderViews << m_cwlcompositor->m_launcherView;
    }

    const int winHeight = height() > 0 ? height() : 1280;
    const float scale = m_cwlcompositor->scaleFactor();

    for (CwlView *view : renderViews) {
        if (!view) continue;
        
        bool isToplevel = view->isToplevel();
        QString appId = isToplevel ? view->getAppId() : QString();

        if (appId == "cutie-launcher") {
            m_textureBlitter.setOpacity(1.0 - (m_cwlcompositor->m_launcherView->getPosition().y() * scale / winHeight));
        } else if (isToplevel) {
            if (m_cwlcompositor->launcherPosition() > 0.0) {
                m_textureBlitter.setOpacity(m_cwlcompositor->blur() * m_cwlcompositor->m_launcherView->getPosition().y() * scale / winHeight);
            } else {
                m_textureBlitter.setOpacity(m_cwlcompositor->blur());
            }
        } else {
            m_textureBlitter.setOpacity(1.0);
        }

        if (m_cwlcompositor->launcherPosition() == 1.0 && view->layer == CwlViewLayer::TOP)
            continue;

        renderView(view);
    }

    m_textureBlitter.release();

    if (!m_boostActive && m_cwlcompositor) {
        m_cwlcompositor->endRender();
    }
}

void GlWindow::renderView(CwlView *view)
{
    if (!view) return;
    QOpenGLTexture *texture = view->getTexture();
    if (!texture) return;
    
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
        m_textureBlitter.blit(texture->textureId(), targetTransform, surfaceOrigin);
    }

    for (CwlView *childView : view->getChildViews()) {
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
    m_gesture->handlePointerEvent(ev, [this, btn](QList<QEventPoint> points) {
        m_cwlcompositor->handleMousePressEvent(points, btn);
    });
}

void GlWindow::mouseReleaseEvent(QMouseEvent *ev)
{
    if (!m_gesture || !m_cwlcompositor) return;
    startBoost();
    Qt::MouseButton btn = ev->button();
    m_gesture->handlePointerEvent(ev, [this, btn](QList<QEventPoint> points) {
        m_cwlcompositor->handleMouseReleaseEvent(points, btn);
    });
}

void GlWindow::keyPressEvent(QKeyEvent *event)
{
    if (!m_cwlcompositor) return;
    if (event->key() == Qt::Key_PowerOff)
        m_cwlcompositor->specialKey(CutieShell::SpecialKey::POWER_PRESS);

    if (event->key() == Qt::Key_VolumeUp)
        m_cwlcompositor->specialKey(CutieShell::SpecialKey::VOLUME_UP_PRESS);

    if (event->key() == Qt::Key_VolumeDown)
        m_cwlcompositor->specialKey(CutieShell::SpecialKey::VOLUME_DOWN_PRESS);
}

void GlWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (!m_cwlcompositor) return;
    if (event->key() == Qt::Key_PowerOff)
        m_cwlcompositor->specialKey(CutieShell::SpecialKey::POWER_RELEASE);

    if (event->key() == Qt::Key_VolumeUp)
        m_cwlcompositor->specialKey(CutieShell::SpecialKey::VOLUME_UP_RELEASE);

    if (event->key() == Qt::Key_VolumeDown)
        m_cwlcompositor->specialKey(CutieShell::SpecialKey::VOLUME_DOWN_RELEASE);
}