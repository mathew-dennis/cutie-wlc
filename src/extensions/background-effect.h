#ifndef BACKGROUND_EFFECT_H
#define BACKGROUND_EFFECT_H

#include <QtWaylandCompositor/QWaylandCompositorExtensionTemplate>
#include <QtWaylandCompositor/QWaylandCompositor>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QHash>
#include <QRegion>

#include "qwayland-server-ext-background-effect-v1.h"
#include <cutie-wlc.h>

class BackgroundEffectSurfaceV1;

class BackgroundEffectManagerV1
	: public QWaylandCompositorExtensionTemplate<BackgroundEffectManagerV1>,
	  public QtWaylandServer::ext_background_effect_manager_v1 {
	Q_OBJECT
    public:
	BackgroundEffectManagerV1(CwlCompositor *compositor);
	void initialize() override;

	// Committed blur region of a surface, in surface-local coordinates.
	QRegion blurRegion(QWaylandSurface *surface) const;

    protected:
	void ext_background_effect_manager_v1_bind_resource(
		Resource *resource) override;
	void ext_background_effect_manager_v1_destroy(
		Resource *resource) override;
	void ext_background_effect_manager_v1_get_background_effect(
		Resource *resource, uint32_t id,
		struct ::wl_resource *surface) override;

    private:
	friend class BackgroundEffectSurfaceV1;
	CwlCompositor *m_compositor;
	QHash<QWaylandSurface *, BackgroundEffectSurfaceV1 *> m_effects;
};

class BackgroundEffectSurfaceV1
	: public QObject,
	  public QtWaylandServer::ext_background_effect_surface_v1 {
	Q_OBJECT
    public:
	BackgroundEffectSurfaceV1(BackgroundEffectManagerV1 *manager,
				  QWaylandSurface *surface,
				  struct ::wl_client *client, uint32_t id,
				  int version);

	QRegion region() const;

    protected:
	void ext_background_effect_surface_v1_destroy_resource(
		Resource *resource) override;
	void ext_background_effect_surface_v1_destroy(
		Resource *resource) override;
	void ext_background_effect_surface_v1_set_blur_region(
		Resource *resource, struct ::wl_resource *region) override;

    private slots:
	void onCommit();
	void onSurfaceDestroyed();

    private:
	BackgroundEffectManagerV1 *m_manager;
	QWaylandSurface *m_surface;
	QRegion m_pending;
	QRegion m_region;
	bool m_hasPending = false;
};

#endif
