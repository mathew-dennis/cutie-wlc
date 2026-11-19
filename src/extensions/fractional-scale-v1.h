#ifndef FRACTIONALSCALE
#define FRACTIONALSCALE

#include "wayland-util.h"

#include <QtWaylandCompositor/QWaylandCompositorExtensionTemplate>
#include <QtWaylandCompositor/QWaylandCompositor>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QHash>

#include <cutie-wlc.h>

#include "qwayland-server-fractional-scale-v1.h"

class FractionalScaleV1;
class FractionalScaleManagerV1
	: public QWaylandCompositorExtensionTemplate<FractionalScaleManagerV1>,
	  public QtWaylandServer::wp_fractional_scale_manager_v1 {
	Q_OBJECT
    public:
	FractionalScaleManagerV1(CwlCompositor *compositor);
	void initialize() override;
	void setSurfaceScale(QWaylandSurface *surface, int scale);

    protected:
	void wp_fractional_scale_manager_v1_destroy(Resource *resource) override;
	void wp_fractional_scale_manager_v1_get_fractional_scale(
		Resource *resource, uint32_t id,
		struct ::wl_resource *surface) override;

    private:
	friend class FractionalScaleV1;
	CwlCompositor *m_compositor;
	QHash<QWaylandSurface *, FractionalScaleV1 *> m_objects;
};

class FractionalScaleV1 : public QObject,
			  public QtWaylandServer::wp_fractional_scale_v1 {
	Q_OBJECT
    public:
	FractionalScaleV1(struct ::wl_client *client, uint32_t id, int version,
			  QWaylandSurface *surface,
			  FractionalScaleManagerV1 *manager);
	void setScale(int scale);

    protected:
	void wp_fractional_scale_v1_destroy(Resource *resource) override;
	void wp_fractional_scale_v1_destroy_resource(Resource *resource) override;

    private:
	void detach();

	FractionalScaleManagerV1 *m_manager;
	QWaylandSurface *m_surface;
	int m_scale = 0;
};

#endif //FRACTIONALSCALE
