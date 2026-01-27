/*
 * Compile with:
 * gcc -o drag drag.c xdg-shell-client-protocol.c -I. -lwayland-client -lrt
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h" 

/* --- Shared Memory Helper --- */
static int os_create_anonymous_file(off_t size) {
    int fd = memfd_create("wayland-shm", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) { close(fd); return -1; }
    return fd;
}

static struct wl_buffer *create_buffer(struct wl_shm *shm, int w, int h, uint32_t color) {
    int stride = w * 4;
    int size = stride * h;
    int fd = os_create_anonymous_file(size);
    if (fd < 0) return NULL;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return NULL; }

    for (int i = 0; i < w * h; i++) data[i] = color;

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    return buffer;
}

char* create_uri_list(const char *path) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "file://%s\r\n", path);
    return strdup(buffer);
}

/* --- Wayland Context --- */
struct context {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_data_device_manager *data_device_manager;
    struct wl_pointer *pointer;
    struct wl_data_device *data_device;
    
    struct wl_surface *surface;
    
    struct xdg_wm_base *wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    char *payload_data;
    int running;
    int configured;
};

/* --- XDG Shell Listeners --- */
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct context *ctx = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    
    if (!ctx->configured) {
        struct wl_buffer *buffer = create_buffer(ctx->shm, 200, 200, 0xFF00FF00); // Green
        wl_surface_attach(ctx->surface, buffer, 0, 0);
        wl_surface_commit(ctx->surface);
        ctx->configured = 1;
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
    (void)data; (void)xdg_toplevel; (void)width; (void)height; (void)states;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    (void)xdg_toplevel;
    struct context *ctx = data;
    ctx->running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};


/* --- Data Source Handlers --- */
static void data_source_target(void *data, struct wl_data_source *source, const char *mime_type) {
    (void)data; (void)source; (void)mime_type;
}

static void data_source_send(void *data, struct wl_data_source *source, const char *mime_type, int32_t fd) {
    (void)source;
    struct context *ctx = data;
    if (strcmp(mime_type, "text/uri-list") == 0) {
        dprintf(fd, "%s", ctx->payload_data);
    }
    close(fd);
}

static void data_source_cancelled(void *data, struct wl_data_source *source) {
    (void)data;
    printf("Drag cancelled.\n");
    wl_data_source_destroy(source);
}

static void data_source_dnd_drop_performed(void *data, struct wl_data_source *source) {
    (void)data; (void)source;
    printf("Drop performed!\n");
}

static void data_source_dnd_finished(void *data, struct wl_data_source *source) {
    (void)data;
    printf("Drag finished completely.\n");
    wl_data_source_destroy(source);
}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_target,
    .send = data_source_send,
    .cancelled = data_source_cancelled,
    .dnd_drop_performed = data_source_dnd_drop_performed,
    .dnd_finished = data_source_dnd_finished,
    .action = NULL, 
};

/* --- Pointer Handlers --- */
static void pointer_enter(void *data, struct wl_pointer *ptr, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)ptr; (void)serial; (void)s; (void)x; (void)y;
}

static void pointer_leave(void *data, struct wl_pointer *ptr, uint32_t serial, struct wl_surface *s) {
    (void)data; (void)ptr; (void)serial; (void)s;
}

static void pointer_motion(void *data, struct wl_pointer *ptr, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)ptr; (void)time; (void)x; (void)y;
}

static void pointer_button(void *data, struct wl_pointer *ptr, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    (void)ptr; (void)time;
    struct context *ctx = data;
    
    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        printf("Click detected. Starting Drag...\n");
        
        struct wl_data_source *source = wl_data_device_manager_create_data_source(ctx->data_device_manager);
        wl_data_source_add_listener(source, &data_source_listener, ctx);
        wl_data_source_offer(source, "text/uri-list");
        
        struct wl_surface *icon_surface = wl_compositor_create_surface(ctx->compositor);
        struct wl_buffer *buffer = create_buffer(ctx->shm, 50, 50, 0xFF0000FF); // Blue Icon
        wl_surface_attach(icon_surface, buffer, 0, 0);
        wl_surface_commit(icon_surface);

        wl_data_device_start_drag(ctx->data_device, source, ctx->surface, icon_surface, serial);
    }
}

static void pointer_axis(void *data, struct wl_pointer *ptr, uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)data; (void)ptr; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *wl_pointer) { (void)data; (void)wl_pointer; }
static void pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) { (void)data; (void)wl_pointer; (void)axis_source; }
static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) { (void)data; (void)wl_pointer; (void)time; (void)axis; }
static void pointer_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) { (void)data; (void)wl_pointer; (void)axis; (void)discrete; }

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_discrete, // Fixed here
};

/* --- Seat Listener --- */
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct context *ctx = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ctx->pointer) {
        ctx->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(ctx->pointer, &pointer_listener, ctx);
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) { (void)data; (void)seat; (void)name; }
static const struct wl_seat_listener seat_listener = { seat_capabilities, seat_name };

/* --- Registry Listener --- */
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    struct context *ctx = data;
    if (strcmp(interface, "wl_compositor") == 0) {
        ctx->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_shm") == 0) {
        ctx->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "wl_seat") == 0) {
        ctx->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
    } else if (strcmp(interface, "wl_data_device_manager") == 0) {
        ctx->data_device_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        ctx->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(ctx->wm_base, &xdg_wm_base_listener, ctx);
    }
}
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) { (void)data; (void)registry; (void)name; }
static const struct wl_registry_listener registry_listener = { registry_global, registry_global_remove };


int main(int argc, char **argv) {
    if (argc < 2) { printf("Usage: %s <file_path>\n", argv[0]); return 1; }

    struct context ctx = {0};
    ctx.payload_data = create_uri_list(argv[1]);
    ctx.running = 1;
    ctx.configured = 0;

    ctx.display = wl_display_connect(NULL);
    if (!ctx.display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return 1;
    }

    ctx.registry = wl_display_get_registry(ctx.display);
    wl_registry_add_listener(ctx.registry, &registry_listener, &ctx);
    
    wl_display_roundtrip(ctx.display);

    if (!ctx.compositor || !ctx.shm || !ctx.seat || !ctx.data_device_manager || !ctx.wm_base) {
        fprintf(stderr, "Error: Missing required Wayland interfaces.\n");
        return 1;
    }

    ctx.data_device = wl_data_device_manager_get_data_device(ctx.data_device_manager, ctx.seat);

    /* --- Create Window using XDG Shell --- */
    ctx.surface = wl_compositor_create_surface(ctx.compositor);
    
    ctx.xdg_surface = xdg_wm_base_get_xdg_surface(ctx.wm_base, ctx.surface);
    xdg_surface_add_listener(ctx.xdg_surface, &xdg_surface_listener, &ctx);
    
    ctx.xdg_toplevel = xdg_surface_get_toplevel(ctx.xdg_surface);
    xdg_toplevel_add_listener(ctx.xdg_toplevel, &xdg_toplevel_listener, &ctx);
    xdg_toplevel_set_title(ctx.xdg_toplevel, "Drag Source (Green Box)");

    wl_surface_commit(ctx.surface);

    printf("Wayland DnD Source.\nClick the GREEN box to start dragging the BLUE box.\n");

    while (ctx.running && wl_display_dispatch(ctx.display) != -1) {
        // Event loop
    }

    return 0;
}
