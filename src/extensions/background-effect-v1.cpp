#include <background-effect-v1.h>
#include <glwindow.h>

BackgroundEffectManagerV1::BackgroundEffectManagerV1(CwlCompositor *compositor)
	: QWaylandCompositorExtensionTemplate(compositor)
	, m_compositor(compositor)
{
}

void BackgroundEffectManagerV1::initialize()
{
	QWaylandCompositorExtensionTemplate::initialize();
	init(m_compositor->display(), 1);
}

bool BackgroundEffectManagerV1::hasBlur(QWaylandSurface *surface) const
{
	BackgroundEffectSurfaceV1 *object = m_objects.value(surface);
	return object && object->blur();
}

void BackgroundEffectManagerV1::ext_background_effect_manager_v1_bind_resource(
	Resource *resource)
{
	// Only offered when GlWindow could set up the blur
	send_capabilities(resource->handle,
			  m_compositor->glWindow()->supportsBackgroundBlur() ?
				  capability_blur :
				  0);
}

void BackgroundEffectManagerV1::ext_background_effect_manager_v1_destroy(
	Resource *resource)
{
	wl_resource_destroy(resource->handle);
}

void BackgroundEffectManagerV1::
	ext_background_effect_manager_v1_get_background_effect(
		Resource *resource, uint32_t id,
		struct ::wl_resource *surfaceResource)
{
	QWaylandSurface *surface =
		QWaylandSurface::fromResource(surfaceResource);
	if (!surface)
		return;

	if (m_objects.contains(surface)) {
		wl_resource_post_error(
			resource->handle, error_background_effect_exists,
			"the surface already has a background effect object");
		return;
	}

	m_objects.insert(surface, new BackgroundEffectSurfaceV1(
					  resource->client(), id,
					  resource->version(), surface, this));
}

BackgroundEffectSurfaceV1::BackgroundEffectSurfaceV1(
	struct ::wl_client *client, uint32_t id, int version,
	QWaylandSurface *surface, BackgroundEffectManagerV1 *manager)
	: QtWaylandServer::ext_background_effect_surface_v1(client, id, version)
	, m_manager(manager)
	, m_surface(surface)
{
	connect(surface, &QWaylandSurface::surfaceDestroyed, this,
		&BackgroundEffectSurfaceV1::detach);
	// redraw is emitted on wl_surface.commit, like layer-shell relies on
	connect(surface, &QWaylandSurface::redraw, this,
		&BackgroundEffectSurfaceV1::onCommit);
}

// The commit that applies this also makes the compositor redraw the surface
// (CwlView::onRedraw), so nothing has to ask for a frame here.
void BackgroundEffectSurfaceV1::onCommit()
{
	m_blur = m_pendingBlur;
}

void BackgroundEffectSurfaceV1::detach()
{
	if (m_surface && m_manager->m_objects.value(m_surface) == this)
		m_manager->m_objects.remove(m_surface);
	m_surface = nullptr;
	m_blur = false;
}

void BackgroundEffectSurfaceV1::ext_background_effect_surface_v1_set_blur_region(
	Resource *resource, struct ::wl_resource *region)
{
	if (!m_surface) {
		wl_resource_post_error(resource->handle,
				       error_surface_destroyed,
				       "the surface was destroyed");
		return;
	}
	// A NULL region removes the effect. The shape of a region is not read
	// yet: any region blurs the whole surface.
	m_pendingBlur = region != nullptr;
}

void BackgroundEffectSurfaceV1::ext_background_effect_surface_v1_destroy(
	Resource *resource)
{
	wl_resource_destroy(resource->handle);
}

void BackgroundEffectSurfaceV1::ext_background_effect_surface_v1_destroy_resource(
	Resource *resource)
{
	// The spec removes the effect on the next commit, here it goes at
	// once. The difference is at most one frame.
	const bool wasBlurred = m_blur;
	detach();
	if (wasBlurred)
		m_manager->m_compositor->triggerRender();
	deleteLater();
}
