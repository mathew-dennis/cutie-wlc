#include <fractional-scale-v1.h>

// wp_fractional_scale_v1 sends the scale as a numerator over this denominator.
static const int FRACTIONAL_SCALE_DENOMINATOR = 120;

FractionalScaleManagerV1::FractionalScaleManagerV1(CwlCompositor *compositor)
	: QWaylandCompositorExtensionTemplate(compositor)
	, m_compositor(compositor)
{
}

void FractionalScaleManagerV1::initialize()
{
	QWaylandCompositorExtensionTemplate::initialize();
	init(m_compositor->display(), 1);
}

void FractionalScaleManagerV1::setSurfaceScale(QWaylandSurface *surface,
					       int scale)
{
	if (FractionalScaleV1 *object = m_objects.value(surface))
		object->setScale(scale);
}

void FractionalScaleManagerV1::wp_fractional_scale_manager_v1_destroy(
	Resource *resource)
{
	wl_resource_destroy(resource->handle);
}

void FractionalScaleManagerV1::wp_fractional_scale_manager_v1_get_fractional_scale(
	Resource *resource, uint32_t id, struct ::wl_resource *surfaceResource)
{
	QWaylandSurface *surface = QWaylandSurface::fromResource(surfaceResource);
	if (!surface)
		return;

	if (m_objects.contains(surface)) {
		wl_resource_post_error(
			resource->handle, error_fractional_scale_exists,
			"the surface already has a fractional_scale object");
		return;
	}

	FractionalScaleV1 *object = new FractionalScaleV1(
		resource->client(), id, resource->version(), surface, this);
	m_objects.insert(surface, object);

	// The output of the surface is not known yet, so start with the scale
	// of the primary output. GlWindow corrects it when the surface is drawn
	// on another output.
	object->setScale(m_compositor->scaleFactor());
}

FractionalScaleV1::FractionalScaleV1(struct ::wl_client *client, uint32_t id,
				     int version, QWaylandSurface *surface,
				     FractionalScaleManagerV1 *manager)
	: QtWaylandServer::wp_fractional_scale_v1(client, id, version)
	, m_manager(manager)
	, m_surface(surface)
{
	connect(surface, &QWaylandSurface::surfaceDestroyed, this,
		&FractionalScaleV1::detach);
}

void FractionalScaleV1::detach()
{
	if (m_surface && m_manager->m_objects.value(m_surface) == this)
		m_manager->m_objects.remove(m_surface);
	m_surface = nullptr;
}

void FractionalScaleV1::setScale(int scale)
{
	scale = qMax(1, scale);
	if (!m_surface || scale == m_scale)
		return;
	m_scale = scale;
	send_preferred_scale(scale * FRACTIONAL_SCALE_DENOMINATOR);
}

void FractionalScaleV1::wp_fractional_scale_v1_destroy(Resource *resource)
{
	wl_resource_destroy(resource->handle);
}

void FractionalScaleV1::wp_fractional_scale_v1_destroy_resource(
	Resource *resource)
{
	detach();
	deleteLater();
}
