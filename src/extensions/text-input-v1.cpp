#include <text-input-v1.h>

#include <QWaylandSeat>

TextInputManagerV1::TextInputManagerV1()
{
}

TextInputManagerV1::TextInputManagerV1(CwlCompositor *compositor)
	: QWaylandCompositorExtensionTemplate(compositor)
	, m_compositor(compositor)
{
}

void TextInputManagerV1::initialize()
{
	QWaylandCompositorExtensionTemplate::initialize();
	init(m_compositor->display(), 1);
}

void TextInputManagerV1::zwp_text_input_manager_v1_create_text_input(
	Resource *resource, uint32_t id)
{
	TextInputV1 *textInput = new TextInputV1(
		resource->client(), id, resource->version(), m_compositor);

	connect(textInput, &TextInputV1::showInputPanel, this,
		&TextInputManagerV1::showInputPanel);
	connect(textInput, &TextInputV1::hideInputPanel, this,
		&TextInputManagerV1::hideInputPanel);
	connect(textInput, &TextInputV1::contentTypeChanged, this,
		&TextInputManagerV1::contentTypeChanged);
}

TextInputV1::TextInputV1(wl_client *client, uint32_t id, int version,
			 CwlCompositor *compositor)
	: QtWaylandServer::zwp_text_input_v1(client, id, version)
{
	m_compositor = compositor;
	connect(m_compositor->defaultSeat(),
		&QWaylandSeat::keyboardFocusChanged, this,
		&TextInputV1::onKeyboardFocusChanged);
}

void TextInputV1::zwp_text_input_v1_bind_resource(Resource *resource)
{
}

void TextInputV1::zwp_text_input_v1_destroy_resource(Resource *resource)
{
	if (m_hasFocus) {
		send_leave();
		emit hideInputPanel();
	}
	if (m_activeSurface)
		if (CwlView *view =
			    m_compositor->findTlView(m_activeSurface))
			if (view->tiV1 == this)
				view->tiV1 = nullptr;
	m_hasFocus = false;
	m_activeSurface = nullptr;
}

void TextInputV1::zwp_text_input_v1_activate(Resource *resource,
					     struct ::wl_resource *seat,
					     struct ::wl_resource *surface)
{
	QWaylandSurface *requestedSurface =
		QWaylandSurface::fromResource(surface);
	if (!requestedSurface || !requestedSurface->client() ||
	    requestedSurface->client()->client() != resource->client())
		return;
	if (m_activeSurface != requestedSurface) {
		if (m_hasFocus) {
			send_leave();
			emit hideInputPanel();
		}
		m_activeSurface = requestedSurface;
		m_hasFocus = false;
		connect(requestedSurface, &QObject::destroyed, this,
			[this, requestedSurface]() {
				if (m_activeSurface == requestedSurface) {
					m_activeSurface = nullptr;
					m_hasFocus = false;
				}
			});
	}
	if (m_compositor->defaultSeat()->keyboardFocus() == requestedSurface)
		onKeyboardFocusChanged(requestedSurface, nullptr);
}

void TextInputV1::zwp_text_input_v1_deactivate(Resource *resource,
					       struct ::wl_resource *seat)
{
	if (m_hasFocus) {
		send_leave();
		emit hideInputPanel();
	}
	if (m_activeSurface)
		if (CwlView *view =
			    m_compositor->findTlView(m_activeSurface))
			if (view->tiV1 == this)
				view->tiV1 = nullptr;
	m_hasFocus = false;
	m_activeSurface = nullptr;
}

void TextInputV1::zwp_text_input_v1_show_input_panel(Resource *resource)
{
	QWaylandSurface *surface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (!surface || surface != m_activeSurface || !m_hasFocus ||
	    !surface->client() ||
	    surface->client()->client() != resource->client())
		return;
	CwlView *view = m_compositor->findTlView(surface);
	if (!view)
		return;
	view->tiV1 = this;
	emit showInputPanel();
}

void TextInputV1::zwp_text_input_v1_hide_input_panel(Resource *resource)
{
	QWaylandSurface *surface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (!surface || surface != m_activeSurface || !m_hasFocus ||
	    !surface->client() ||
	    surface->client()->client() != resource->client())
		return;
	emit hideInputPanel();
}

void TextInputV1::zwp_text_input_v1_reset(Resource *resource)
{
}

void TextInputV1::zwp_text_input_v1_set_surrounding_text(Resource *resource,
							 const QString &text,
							 uint32_t cursor,
							 uint32_t anchor)
{
}

void TextInputV1::zwp_text_input_v1_set_content_type(Resource *resource,
						     uint32_t hint,
						     uint32_t purpose)
{
	QWaylandSurface *surface =
		m_compositor->defaultSeat()->keyboardFocus();
	if (!surface || surface != m_activeSurface || !m_hasFocus ||
	    !surface->client() ||
	    surface->client()->client() != resource->client())
		return;
	emit contentTypeChanged(hint, purpose);
}

void TextInputV1::onKeyboardFocusChanged(QWaylandSurface *newFocus,
						 QWaylandSurface *oldFocus)
{
	if (m_hasFocus && oldFocus == m_activeSurface &&
	    newFocus != m_activeSurface) {
		send_leave();
		emit hideInputPanel();
		if (CwlView *view = m_compositor->findTlView(oldFocus))
			if (view->tiV1 == this)
				view->tiV1 = nullptr;
		m_hasFocus = false;
	}
	if (newFocus != m_activeSurface || m_hasFocus)
		return;
	CwlView *view = m_compositor->findTlView(newFocus);
	if (!view)
		return;
	view->tiV1 = this;
	send_enter(newFocus->resource());
	m_hasFocus = true;
}

void TextInputV1::zwp_text_input_v1_set_cursor_rectangle(Resource *resource,
							 int32_t x, int32_t y,
							 int32_t width,
							 int32_t height)
{
}

void TextInputV1::zwp_text_input_v1_set_preferred_language(
	Resource *resource, const QString &language)
{
}

void TextInputV1::zwp_text_input_v1_commit_state(Resource *resource,
						 uint32_t serial)
{
}

void TextInputV1::zwp_text_input_v1_invoke_action(Resource *resource,
						  uint32_t button,
						  uint32_t index)
{
}
