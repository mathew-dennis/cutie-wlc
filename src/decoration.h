#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>

// Server-side window decoration used for windows on external screens.
// All sizes are in logical pixels.
namespace CwlDecoration {
constexpr int TitleBarHeight = 32;
constexpr int ButtonWidth = 40;
// Invisible grab band around a windowed window, outside of the frame
constexpr int ResizeBorder = 6;
// Visible border drawn around the frame of a windowed window
constexpr int BorderWidth = 2;

inline QColor borderColor(bool active)
{
	return active ? QColor("#5a8dee") : QColor("#3a3f47");
}

// Pointer shapes drawn by the compositor. CursorClient means the client's
// own cursor is used.
enum CursorShape {
	CursorClient = 0,
	CursorArrow,
	CursorResizeH,
	CursorResizeV,
	CursorResizeNWSE,
	CursorResizeNESW,
	CursorShapeCount
};

enum Part {
	PartNone = 0,
	PartTitle,
	PartMinimize,
	PartMaximize,
	PartClose,
	PartResize
};

struct Hit {
	int part = PartNone;
	Qt::Edges edges;
};

inline QSize minimumWindowSize()
{
	return QSize(240, 160);
}

// Title bar sits directly above the window content
QRectF titleBarRect(const QRectF &content);
QRectF frameRect(const QRectF &content);

// resizable: windowed windows can be resized from their edges
Hit hitTest(const QRectF &content, const QPointF &point, bool resizable);

CursorShape cursorForEdges(Qt::Edges edges);
QImage cursorImage(int shape);
QPoint cursorHotspot(int shape);

// hover: the Part under the pointer, or PartNone
QImage paintTitleBar(const QSize &pixelSize, qreal scale, const QString &title,
		     bool active, bool maximized, int hover);
} // namespace CwlDecoration
