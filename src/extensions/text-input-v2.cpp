#include <text-input-v2.h>

#include <QWaylandSeat>

TextInputManagerV2::TextInputManagerV2()
{
}

TextInputManagerV2::TextInputManagerV2(CwlCompositor *compositor)
	: QWaylandCompositorExtensionTemplate(compositor)
	, m_compositor(compositor)
{
}

void TextInputManagerV2::initialize()
{
	QWaylandCompositorExtensionTemplate::initialize();
	init(m_compositor->display(), 1);
}

void TextInputManagerV2::zwp_text_input_manager_v2_get_text_input(
	Resource *resource, uint32_t id, struct ::wl_resource *seat)
{
	QWaylandSurface *surface;
	TextInputV2 *textInput = new TextInputV2(
		resource->client(), id, resource->version(), m_compositor);

	connect(textInput, &TextInputV2::showInputPanel, this,
		&TextInputManagerV2::showInputPanel);
	connect(textInput, &TextInputV2::hideInputPanel, this,
		&TextInputManagerV2::hideInputPanel);
	connect(textInput, &TextInputV2::contentTypeChanged, this,
		&TextInputManagerV2::contentTypeChanged);

	if (m_compositor->defaultSeat()->keyboardFocus() != nullptr) {
		surface = m_compositor->defaultSeat()->keyboardFocus();
		if (surface->client()->client() == resource->client()) {
			textInput->send_enter(0, surface->resource());
			if (CwlView *view = m_compositor->findTlView(surface))
				view->tiV2 = textInput;
		}
	}
}

TextInputV2::TextInputV2(wl_client *client, uint32_t id, int version,
			 CwlCompositor *compositor)
	: QtWaylandServer::zwp_text_input_v2(client, id, version)
{
	m_compositor = compositor;
	connect(m_compositor->defaultSeat(),
		&QWaylandSeat::keyboardFocusChanged, this,
		&TextInputV2::onKeyboardFocusChanged);
}

void TextInputV2::zwp_text_input_v2_show_input_panel(Resource *resource)
{
	QWaylandSurface *surface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (!surface || !surface->client() ||
	    surface->client()->client() != resource->client() ||
	    !m_enabledSurfaces.contains(surface))
		return;
	CwlView *view = m_compositor->findTlView(surface);
	if (!view)
		return;
	view->tiV2 = this;
	emit showInputPanel();
}

void TextInputV2::zwp_text_input_v2_hide_input_panel(Resource *resource)
{
	QWaylandSurface *surface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (!surface || !surface->client() ||
	    surface->client()->client() != resource->client())
		return;
	emit hideInputPanel();
}

void TextInputV2::zwp_text_input_v2_set_content_type(Resource *resource,
						     uint32_t hint,
						     uint32_t purpose)
{
	QWaylandSurface *surface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (!surface || !surface->client() ||
	    surface->client()->client() != resource->client())
		return;
	emit contentTypeChanged(hint, purpose);
}

void TextInputV2::zwp_text_input_v2_destroy(Resource *resource)
{
	qDebug() << "zwp_text_input_v2_destroy";
}

void TextInputV2::zwp_text_input_v2_destroy_resource(Resource *resource)
{
	qDebug() << "zwp_text_input_v2_destroy_resource";
	QWaylandSurface *surface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (surface && m_enabledSurfaces.contains(surface))
		emit hideInputPanel();
	for (QWaylandSurface *enabledSurface : m_enabledSurfaces)
		if (CwlView *view =
			    m_compositor->findTlView(enabledSurface))
			if (view->tiV2 == this)
				view->tiV2 = nullptr;
	m_enabledSurfaces.clear();
}

void TextInputV2::zwp_text_input_v2_enable(Resource *resource,
					   struct ::wl_resource *surface)
{
	QWaylandSurface *requestedSurface =
		QWaylandSurface::fromResource(surface);
	QWaylandSurface *focusedSurface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (!requestedSurface || !requestedSurface->client() ||
	    requestedSurface->client()->client() != resource->client())
		return;
	if (!m_enabledSurfaces.contains(requestedSurface)) {
		m_enabledSurfaces.insert(requestedSurface);
		connect(requestedSurface, &QObject::destroyed, this,
			[this, requestedSurface]() {
				m_enabledSurfaces.remove(requestedSurface);
			});
	}
	if (requestedSurface == focusedSurface) {
		if (CwlView *view = m_compositor->findTlView(focusedSurface)) {
			view->tiV2 = this;
			emit showInputPanel();
		}
	}
}

void TextInputV2::zwp_text_input_v2_disable(Resource *resource,
					    struct ::wl_resource *surface)
{
	QWaylandSurface *requestedSurface =
		QWaylandSurface::fromResource(surface);
	QWaylandSurface *focusedSurface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (!requestedSurface || !requestedSurface->client() ||
	    requestedSurface->client()->client() != resource->client())
		return;
	const bool wasEnabled = m_enabledSurfaces.remove(requestedSurface) > 0;
	if (wasEnabled) {
		if (CwlView *view =
			    m_compositor->findTlView(requestedSurface))
			if (view->tiV2 == this)
				view->tiV2 = nullptr;
		if (requestedSurface == focusedSurface)
			emit hideInputPanel();
	}
}

void TextInputV2::onKeyboardFocusChanged(QWaylandSurface *newFocus,
						 QWaylandSurface *oldFocus)
{
	if (oldFocus && oldFocus != newFocus &&
	    m_enabledSurfaces.contains(oldFocus)) {
		if (CwlView *view = m_compositor->findTlView(oldFocus))
			if (view->tiV2 == this)
				view->tiV2 = nullptr;
	}

	if (!newFocus || !m_enabledSurfaces.contains(newFocus))
		return;
	if (CwlView *view = m_compositor->findTlView(newFocus)) {
		view->tiV2 = this;
		emit showInputPanel();
	}
}

void TextInputV2::zwp_text_input_v2_set_surrounding_text(Resource *resource,
							 const QString &text,
							 int32_t cursor,
							 int32_t anchor)
{
	qDebug() << "zwp_text_input_v2_set_surrounding_text";
}

void TextInputV2::zwp_text_input_v2_set_cursor_rectangle(Resource *resource,
							 int32_t x, int32_t y,
							 int32_t width,
							 int32_t height)
{
	qDebug() << "zwp_text_input_v2_set_cursor_rectangle";
}

void TextInputV2::zwp_text_input_v2_set_preferred_language(
	Resource *resource, const QString &language)
{
	qDebug() << "zwp_text_input_v2_set_preferred_language";
}

void TextInputV2::zwp_text_input_v2_update_state(Resource *resource,
						 uint32_t serial,
						 uint32_t reason)
{
	qDebug() << "zwp_text_input_v2_update_state";
}
