#ifndef LAYER_SHELL
#define LAYER_SHELL

#include "wayland-util.h"

#include <QtWaylandCompositor/QWaylandCompositorExtensionTemplate>
#include <QtWaylandCompositor/QWaylandCompositor>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QPropertyAnimation>

#include "qwayland-server-wlr-layer-shell-unstable-v1.h"

class LayerSurfaceV1;

class LayerShellV1 : public QWaylandCompositorExtensionTemplate<LayerShellV1>,
		     public QtWaylandServer::zwlr_layer_shell_v1

{
	Q_OBJECT

    public:
	LayerShellV1(QWaylandCompositor *compositor);
	void initialize() override;

    signals:
	void layerShellSurfaceCreated(LayerSurfaceV1 *layerSurface);

	void layerShellSurfaceTopCreated(struct ::wl_resource *surface);

    public Q_SLOTS:

    protected:
	void zwlr_layer_shell_v1_get_layer_surface(
		Resource *resource, uint32_t id, struct ::wl_resource *surface,
		struct ::wl_resource *output, uint32_t layer,
		const QString &scope) override;

    private:
	QWaylandCompositor *m_compositor = nullptr;
};

class LayerSurfaceV1
	: public QWaylandCompositorExtensionTemplate<LayerSurfaceV1>,
	  public QtWaylandServer::zwlr_layer_surface_v1 {
	Q_OBJECT
	Q_PROPERTY(int32_t targetZone READ targetZone WRITE setTargetZone NOTIFY targetZoneChanged)

    public:
	LayerSurfaceV1(struct ::wl_client *client, uint32_t id, int version);
	QWaylandSurface *surface = nullptr;

	QMargins margins;
	QSize size;
	int ls_zone;
	uint ls_anchor;
	uint32_t ls_layer;
	uint32_t ls_keyboard_interactivity;
	uint32_t ls_serial;
	QString ls_scope;

	QMargins newMargins;
	QSize newSize;
	int new_ls_zone;
	uint new_ls_anchor;
	uint32_t new_ls_layer;
	uint32_t new_ls_keyboard_interactivity;

	bool initialized = false;

	int32_t targetZone();
	void setTargetZone(int32_t targetZone);

    signals:
	void layerSurfaceDestroyed(QWaylandSurface *surface);
	void layerSurfaceDataChanged(LayerSurfaceV1 *surface);
	void targetZoneChanged(int32_t targetZone);
	void hideKeyboard();

    public slots:
	void onCommit();

	private slots:
	void animationValueChanged(const QVariant &value);
	
    private:
    int32_t m_targetZone;
    QPropertyAnimation *targetZoneAnim =
		new QPropertyAnimation(this, "targetZone", this);

    protected:
	void zwlr_layer_surface_v1_set_size(Resource *resource, uint32_t width,
					    uint32_t height) override;
	void zwlr_layer_surface_v1_set_anchor(Resource *resource,
					      uint32_t anchor) override;
	void zwlr_layer_surface_v1_set_exclusive_zone(Resource *resource,
						      int32_t zone) override;
	void zwlr_layer_surface_v1_set_margin(Resource *resource, int32_t top,
					      int32_t right, int32_t bottom,
					      int32_t left) override;
	void zwlr_layer_surface_v1_set_keyboard_interactivity(
		Resource *resource, uint32_t keyboard_interactivity) override;
	void
	zwlr_layer_surface_v1_get_popup(Resource *resource,
					struct ::wl_resource *popup) override;
	void zwlr_layer_surface_v1_ack_configure(Resource *resource,
						 uint32_t serial) override;
	void zwlr_layer_surface_v1_destroy(Resource *resource) override;
	void zwlr_layer_surface_v1_set_layer(Resource *resource,
					     uint32_t layer) override;
};

#endif //LAYER_SHELL
