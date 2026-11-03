#include <decoration.h>

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>

namespace CwlDecoration {

QRectF titleBarRect(const QRectF &content)
{
	return QRectF(content.x(), content.y() - TitleBarHeight,
		      content.width(), TitleBarHeight);
}

QRectF frameRect(const QRectF &content)
{
	return content.adjusted(0, -TitleBarHeight, 0, 0);
}

static QRectF buttonRect(const QRectF &bar, int part)
{
	// Right aligned: minimize, maximize, close
	int slot = part == PartClose ? 1 : part == PartMaximize ? 2 : 3;
	return QRectF(bar.right() - ButtonWidth * slot, bar.y(), ButtonWidth,
		      bar.height());
}

Hit hitTest(const QRectF &content, const QPointF &point, bool resizable)
{
	Hit hit;
	const QRectF bar = titleBarRect(content);
	if (bar.contains(point)) {
		for (int part : { PartClose, PartMaximize, PartMinimize }) {
			if (buttonRect(bar, part).contains(point)) {
				hit.part = part;
				return hit;
			}
		}
		hit.part = PartTitle;
		return hit;
	}

	if (!resizable)
		return hit;
	const QRectF frame = frameRect(content);
	const QRectF outer = frame.adjusted(-ResizeBorder, -ResizeBorder,
					    ResizeBorder, ResizeBorder);
	if (!outer.contains(point) || frame.contains(point))
		return hit;
	if (point.x() < frame.left())
		hit.edges |= Qt::LeftEdge;
	else if (point.x() >= frame.right())
		hit.edges |= Qt::RightEdge;
	if (point.y() < frame.top())
		hit.edges |= Qt::TopEdge;
	else if (point.y() >= frame.bottom())
		hit.edges |= Qt::BottomEdge;
	hit.part = PartResize;
	return hit;
}

QImage paintTitleBar(const QSize &pixelSize, qreal scale, const QString &title,
		     bool active, bool maximized, int hover)
{
	QImage image(pixelSize, QImage::Format_ARGB32);
	const QColor background = active ? QColor("#2b2f36") : QColor("#1f2227");
	const QColor foreground = active ? QColor("#e6e6e6") : QColor("#9aa0a6");
	image.fill(background);

	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	p.scale(scale, scale);
	const QRectF bar(0, 0, pixelSize.width() / scale,
			 pixelSize.height() / scale);

	for (int part : { PartMinimize, PartMaximize, PartClose }) {
		const QRectF r = buttonRect(bar, part);
		if (hover == part)
			p.fillRect(r, part == PartClose ? QColor("#e5484d") :
							  QColor("#3a3f47"));
		const QPointF c = r.center();
		p.setPen(QPen(hover == part && part == PartClose ? Qt::white :
								 foreground,
			      1.4));
		p.setBrush(Qt::NoBrush);
		if (part == PartMinimize) {
			p.drawLine(QPointF(c.x() - 5, c.y() + 3),
				   QPointF(c.x() + 5, c.y() + 3));
		} else if (part == PartMaximize) {
			if (maximized) {
				p.drawRect(QRectF(c.x() - 5, c.y() - 2, 7, 7));
				p.drawPolyline(QPolygonF({ QPointF(c.x() - 2, c.y() - 2),
							   QPointF(c.x() - 2, c.y() - 5),
							   QPointF(c.x() + 5, c.y() - 5),
							   QPointF(c.x() + 5, c.y() + 2),
							   QPointF(c.x() + 2, c.y() + 2) }));
			} else {
				p.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10));
			}
		} else {
			p.drawLine(QPointF(c.x() - 5, c.y() - 5),
				   QPointF(c.x() + 5, c.y() + 5));
			p.drawLine(QPointF(c.x() + 5, c.y() - 5),
				   QPointF(c.x() - 5, c.y() + 5));
		}
	}

	QFont font;
	font.setPixelSize(13);
	p.setFont(font);
	p.setPen(foreground);
	const qreal textWidth = bar.width() - ButtonWidth * 3 - 24;
	if (textWidth > 0) {
		const QString text = QFontMetrics(font).elidedText(
			title, Qt::ElideRight, int(textWidth));
		p.drawText(QRectF(12, 0, textWidth, bar.height()),
			   Qt::AlignVCenter | Qt::AlignLeft, text);
	}
	p.end();
	return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace CwlDecoration
