#include <glwindow.h>

#include <QPainter>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QMouseEvent>

GlWindow::GlWindow()
{
}

void GlWindow::setCompositor(CwlCompositor *cwlcompositor)
{
    m_cwlcompositor = cwlcompositor;
    m_gesture = new CwlGesture(cwlcompositor, QSize(width(), height()));
}

void GlWindow::setAppswitcher(CwlAppswitcher *appswitcher)
{
    m_appswitcher = appswitcher;
    connect(m_appswitcher, &CwlAppswitcher::redraw, this, &GlWindow::requestUpdate);
}

void GlWindow::initializeGL()
{
    m_textureBlitter.create();
    emit glReady();
}

void GlWindow::paintGL()
{
    m_cwlcompositor->startRender();

    QOpenGLFunctions *functions = context()->functions();
    functions->glClearColor(1.f, .6f, .0f, 0.5f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    GLenum currentTarget = GL_TEXTURE_2D;
    m_textureBlitter.bind(currentTarget);
    functions->glEnable(GL_BLEND);
    functions->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int counter = 0;

    QList<CwlView*> renderViews;
    
    if(m_cwlcompositor->m_launcherView != nullptr)
        renderViews = m_cwlcompositor->getViews()<<m_cwlcompositor->m_launcherView;
    else
        renderViews = m_cwlcompositor->getViews();

    for (CwlView *view : renderViews) {
        QString appId;
        if(view != nullptr && view->isToplevel())
            appId = view->getTopLevel()->appId();

        if(!m_cwlcompositor->launcherClosed && !m_cwlcompositor->launcherOpened && view->isToplevel()){
            if(appId == "cutie-launcher"){
                m_textureBlitter.setOpacity(1.0 - (m_cwlcompositor->m_launcherView->getPosition().y()/height()));
            } else {
                m_textureBlitter.setOpacity(m_cwlcompositor->m_launcherView->getPosition().y()/height());
            }
        } else {
            m_textureBlitter.setOpacity(1.0);
        }

        if(m_cwlcompositor->launcherOpened && view->layer == 2)
            continue;

        QOpenGLTexture *texture = view->getTexture();
        if (!texture)
            continue;
        if (texture->target() != currentTarget) {
            currentTarget = texture->target();
            m_textureBlitter.bind(currentTarget);
        }

        QWaylandSurface *surface = view->surface();
        if (surface && surface->hasContent()) {
            QSize viewSize = view->size();
            QPointF viewPosition = view->getPosition();
            auto surfaceOrigin = view->textureOrigin();
            QRectF targetRect;

            if(m_appswitcher != nullptr && m_appswitcher->isActive() && m_appswitcher->getRecentViews().contains(view)){
                QRectF geom = m_appswitcher->getRecentViews().value(view);
                targetRect = QRectF(geom.topLeft(), geom.size());
            } else {
                targetRect = QRectF(
                    viewPosition * m_cwlcompositor->scaleFactor(),
                    viewSize * m_cwlcompositor->scaleFactor()
                );
            }

            QMatrix4x4 targetTransform = QOpenGLTextureBlitter::targetTransform(targetRect, QRect(QPoint(), size()));
            m_textureBlitter.blit(texture->textureId(), targetTransform, surfaceOrigin);
        }

        for (CwlView *childView : view->getChildViews()) {
            QOpenGLTexture *texture = childView->getTexture();
            if (!texture)
                continue;
            if (texture->target() != currentTarget) {
                currentTarget = texture->target();
                m_textureBlitter.bind(currentTarget);
            }

            QWaylandSurface *surface = childView->surface();
            if (surface && surface->hasContent()) {
                QSize viewSize = childView->size();
                QPointF viewPosition = childView->getPosition();
                auto surfaceOrigin = childView->textureOrigin();
                QRectF targetRect = QRectF(
                    viewPosition * m_cwlcompositor->scaleFactor(),
                    viewSize * m_cwlcompositor->scaleFactor()
                );
                QMatrix4x4 targetTransform = QOpenGLTextureBlitter::targetTransform(targetRect, QRect(QPoint(), size()));
                m_textureBlitter.blit(texture->textureId(), targetTransform, surfaceOrigin);
            }
        }
    }

    m_textureBlitter.release();
    m_cwlcompositor->endRender();
}

void GlWindow::touchEvent(QTouchEvent *ev)
{
    m_gesture->handleTouchEvent(ev);
}