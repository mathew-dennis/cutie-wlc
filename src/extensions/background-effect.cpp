#include <background-effect.h>

#include <QtWaylandCompositor/private/qwlregion_p.h>

BackgroundEffectManagerV1::BackgroundEffectManagerV1(
	CwlCompositor *compositor)
	: QWaylandCompositorExtensionTemplate(compositor)
	, m_compositor(compositor)
{
}

void BackgroundEffectManagerV1::initialize()
{
	QWaylandCompositorExtensionTemplate::initialize();
	init(m_compositor->display(), 1);
}

QRegion BackgroundEffectManagerV1::blurRegion(QWaylandSurface *surface) const
{
	BackgroundEffectSurfaceV1 *effect = m_effects.value(surface, nullptr);
	return effect ? effect->region() : QRegion();
}

void BackgroundEffectManagerV1::ext_background_effect_manager_v1_bind_resource(
	Resource *resource)
{
	send_capabilities(resource->handle, capability_blur);
}

void BackgroundEffectManagerV1::ext_background_effect_manager_v1_destroy(
	Resource *resource)
{
	wl_resource_destroy(resource->handle);
}

void BackgroundEffectManagerV1::
	ext_background_effect_manager_v1_get_background_effect(
		Resource *resource, uint32_t id, struct ::wl_resource *surface)
{
	QWaylandSurface *s = QWaylandSurface::fromResource(surface);
	if (m_effects.contains(s)) {
		wl_resource_post_error(
			resource->handle, error_background_effect_exists,
			"the surface already has a background effect object");
		return;
	}

	m_effects.insert(s, new BackgroundEffectSurfaceV1(
				    this, s, resource->client(), id,
				    resource->version()));
}

BackgroundEffectSurfaceV1::BackgroundEffectSurfaceV1(
	BackgroundEffectManagerV1 *manager, QWaylandSurface *surface,
	struct ::wl_client *client, uint32_t id, int version)
	: QtWaylandServer::ext_background_effect_surface_v1(client, id, version)
	, m_manager(manager)
	, m_surface(surface)
{
	connect(surface, &QWaylandSurface::redraw, this,
		&BackgroundEffectSurfaceV1::onCommit);
	connect(surface, &QObject::destroyed, this,
		&BackgroundEffectSurfaceV1::onSurfaceDestroyed);
}

QRegion BackgroundEffectSurfaceV1::region() const
{
	return m_region;
}

void BackgroundEffectSurfaceV1::onCommit()
{
	if (!m_hasPending)
		return;
	m_region = m_pending;
	m_hasPending = false;
	m_manager->m_compositor->triggerRender();
}

void BackgroundEffectSurfaceV1::onSurfaceDestroyed()
{
	// The object becomes inert, see set_blur_region.
	m_manager->m_effects.remove(m_surface);
	m_surface = nullptr;
	m_region = QRegion();
	m_pending = QRegion();
	m_hasPending = false;
}

void BackgroundEffectSurfaceV1::ext_background_effect_surface_v1_destroy_resource(
	Resource *resource)
{
	Q_UNUSED(resource);
	// Spec: the effect is removed on the next commit. We drop it right away.
	if (m_surface) {
		m_manager->m_effects.remove(m_surface);
		m_manager->m_compositor->triggerRender();
	}
	delete this;
}

void BackgroundEffectSurfaceV1::ext_background_effect_surface_v1_destroy(
	Resource *resource)
{
	wl_resource_destroy(resource->handle);
}

void BackgroundEffectSurfaceV1::ext_background_effect_surface_v1_set_blur_region(
	Resource *resource, struct ::wl_resource *region)
{
	if (!m_surface) {
		wl_resource_post_error(resource->handle, error_surface_destroyed,
				       "the associated surface has been destroyed");
		return;
	}
	m_pending = region ? QtWayland::Region::fromResource(region)->region() :
			     QRegion();
	m_hasPending = true;
}
