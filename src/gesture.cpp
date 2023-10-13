#include <gesture.h>
#include <QDebug>

CwlGesture::CwlGesture(CwlCompositor *compositor,  QSize screenSize)
{
	m_cwlcompositor = compositor;
	m_screenSize = screenSize;

	updateGestureRect();
}

CwlGesture::~CwlGesture()
{
}

void CwlGesture::handleTouchEvent(QTouchEvent *ev)
{
	QPointF point;

	if(ev->points().size() != 1){
		corner = CORNER_UNDEFINED;
		edge = EDGE_UNDEFINED;
		m_cwlcompositor->handleTouchEvent(ev);
		return;
	}

	if(ev->isBeginEvent()){
		point = ev->points().first().globalPosition();

		if(m_bottomEdge.contains(point)){
			edge = EDGE_BOTTOM;
		} else if(m_topEdge.contains(point)){
			edge = EDGE_TOP;
		} else if(m_rightEdge.contains(point)){
			edge = EDGE_RIGHT;
		} else if(m_leftEdge.contains(point)){
			edge = EDGE_LEFT;
		} else if(m_tlCorner.contains(point)){
			corner = CORNER_TL;
		} else if(m_trCorner.contains(point)){
			corner = CORNER_TR;
		} else if(m_blCorner.contains(point)){
			corner = CORNER_BL;
		} else if(m_brCorner.contains(point)){
			corner = CORNER_BR;
		} else {
			corner = CORNER_UNDEFINED;
			edge = EDGE_UNDEFINED;
		}

		if(edge<4 || corner<4){
			m_cwlcompositor->handleGesture(ev, edge, corner);
			return;
		} else {
			corner = CORNER_UNDEFINED;
			edge = EDGE_UNDEFINED;
			m_cwlcompositor->handleTouchEvent(ev);
			return;
		}
	}

	if(ev->isUpdateEvent()){
		if(edge<4 || corner<4){
			m_cwlcompositor->handleGesture(ev, edge, corner);
			return;
		} else {
			m_cwlcompositor->handleTouchEvent(ev);
			return;
		}
	}

	if(ev->isEndEvent()){
		if(edge<4 || corner<4){
			m_cwlcompositor->handleGesture(ev, edge, corner);
			corner = CORNER_UNDEFINED;
			edge = EDGE_UNDEFINED;
			return;
		} else {
			m_cwlcompositor->handleTouchEvent(ev);
			return;
		}
		
	}
}

void CwlGesture::updateGestureRect()
{
	m_topEdge = QRect(scaledGestureOffset(), 0, m_screenSize.width() - (2 * scaledGestureOffset()), scaledGestureOffset());
	m_bottomEdge = QRect(scaledGestureOffset(), m_screenSize.height() - scaledGestureOffset(), m_screenSize.width() - (2 * scaledGestureOffset()), scaledGestureOffset());
	m_leftEdge = QRect(0, scaledGestureOffset(), scaledGestureOffset(), m_screenSize.height() - (2 * scaledGestureOffset()));
	m_rightEdge = QRect(m_screenSize.width() - scaledGestureOffset(), scaledGestureOffset(), scaledGestureOffset(), m_screenSize.height() - (2 * scaledGestureOffset()));

	m_tlCorner = QRect(0, 0, scaledGestureOffset(), scaledGestureOffset());
	m_trCorner = QRect(m_screenSize.width() - scaledGestureOffset(), 0, scaledGestureOffset(), scaledGestureOffset());
	m_blCorner = QRect(0, m_screenSize.height() - scaledGestureOffset(), scaledGestureOffset(), scaledGestureOffset());
	m_brCorner = QRect(m_screenSize.width() - scaledGestureOffset(), m_screenSize.height() - scaledGestureOffset(), scaledGestureOffset(), scaledGestureOffset());
}

int CwlGesture::scaledGestureOffset() {
	return m_gestureOffset * m_cwlcompositor->scaleFactor();
}