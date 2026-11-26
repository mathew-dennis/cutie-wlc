#ifndef BACKGROUNDEFFECTV1
#define BACKGROUNDEFFECTV1

#include "wayland-util.h"

#include <QtWaylandCompositor/QWaylandCompositorExtensionTemplate>
#include <QtWaylandCompositor/QWaylandCompositor>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QHash>

#include <cutie-wlc.h>

#include "qwayland-server-ext-background-effect-v1.h"

/*
	ext-background-effect-v1: a client with a translucent surface asks us to
	blur whatever is behind it. This file only keeps track of which surfaces
	asked; GlWindow does the blurring when it draws them.
*/
class BackgroundEffectSurfaceV1;
class BackgroundEffectManagerV1
	: public QWaylandCompositorExtensionTemplate<BackgroundEffectManagerV1>,
	  public QtWaylandServer::ext_background_effect_manager_v1 {
	Q_OBJECT
    public:
	BackgroundEffectManagerV1(CwlCompositor *compositor);
	void initialize() override;
	bool hasBlur(QWaylandSurface *surface) const;

    protected:
	void ext_background_effect_manager_v1_bind_resource(
		Resource *resource) override;
	void
	ext_background_effect_manager_v1_destroy(Resource *resource) override;
	void ext_background_effect_manager_v1_get_background_effect(
		Resource *resource, uint32_t id,
		struct ::wl_resource *surface) override;

    private:
	friend class BackgroundEffectSurfaceV1;
	CwlCompositor *m_compositor;
	QHash<QWaylandSurface *, BackgroundEffectSurfaceV1 *> m_objects;
};

class BackgroundEffectSurfaceV1
	: public QObject,
	  public QtWaylandServer::ext_background_effect_surface_v1 {
	Q_OBJECT
    public:
	BackgroundEffectSurfaceV1(struct ::wl_client *client, uint32_t id,
				  int version, QWaylandSurface *surface,
				  BackgroundEffectManagerV1 *manager);
	bool blur() const
	{
		return m_blur;
	}

    protected:
	void
	ext_background_effect_surface_v1_destroy(Resource *resource) override;
	void ext_background_effect_surface_v1_destroy_resource(
		Resource *resource) override;
	void ext_background_effect_surface_v1_set_blur_region(
		Resource *resource, struct ::wl_resource *region) override;

    private slots:
	void onCommit();

    private:
	void detach();

	BackgroundEffectManagerV1 *m_manager;
	QWaylandSurface *m_surface;
	// The region is double buffered: set_blur_region changes the pending
	// value, the next wl_surface.commit makes it current.
	bool m_pendingBlur = false;
	bool m_blur = false;
};

#endif //BACKGROUNDEFFECTV1
