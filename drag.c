#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

// --- Font/Drawing Helpers (Same as before) ---
static uint8_t font_map[] = { 
    0x3C, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00, 0x00, // A (Placeholder for all chars for simplicity)
}; 
// Note: Real text rendering requires 1000s of lines. 
// For this example, we draw simple blocks or lines for text.

static int create_shm_file(off_t size) {
    int fd = memfd_create("wl-shm", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) { close(fd); return -1; }
    return fd;
}

// --- URI Helper (From your code) ---
char* create_uri_list(const char *path) {
    size_t len = strlen(path);
    char *output = malloc(len * 3 + 16); 
    char *p = output;
    p += sprintf(p, "file://");
    if (path[0] != '/') *p++ = '/';
    for (const char *s = path; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c=='-' || c=='.' || c=='_' || c=='~' || c=='/') *p++ = c;
        else p += sprintf(p, "%%%02X", c);
    }
    *p++ = '\r'; *p++ = '\n'; *p = '\0';
    return output;
}

// --- Wayland State ---
struct State {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_data_device_manager *data_device_manager;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct xdg_wm_base *xdg_wm_base;
    
    struct wl_surface *main_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_data_source *source;
    struct wl_surface *icon_surface;
    
    char *uri_content;
    char *filename;
    uint32_t serial;
    int running;
};

// --- Drawing ---
void draw_buffer(struct State *state, struct wl_surface *surface, int width, int height, uint32_t color) {
    int stride = width * 4;
    int size = stride * height;
    int fd = create_shm_file(size);
    uint32_t *pixels = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    for (int i = 0; i < width * height; ++i) pixels[i] = color;
    
    // Draw pseudo-text (black line) to visualize content
    for(int y=height/2-2; y<height/2+2; y++)
        for(int x=10; x<width-10; x++) pixels[y*width+x] = 0xFF000000;

    munmap(pixels, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, width, height);
    wl_surface_commit(surface);
    wl_buffer_destroy(buffer);
    wl_shm_pool_destroy(pool);
    close(fd);
}

// --- Data Source (The Drag Logic) ---
static void data_source_target(void *data, struct wl_data_source *source, const char *mime) {}

static void data_source_send(void *data, struct wl_data_source *source, const char *mime, int32_t fd) {
    struct State *state = data;
    if (strcmp(mime, "text/uri-list") == 0) {
        printf("Dropping URI: %s\n", state->uri_content);
        write(fd, state->uri_content, strlen(state->uri_content));
    }
    close(fd);
}

static void data_source_cancelled(void *data, struct wl_data_source *source) {
    printf("Drag cancelled.\n");
    wl_data_source_destroy(source);
}

static void data_source_dnd_drop_performed(void *data, struct wl_data_source *source) {}
static void data_source_dnd_finished(void *data, struct wl_data_source *source) {
    printf("Drop finished successfully.\n");
    struct State *state = data;
    wl_data_source_destroy(source);
    state->running = 0; // Exit after drop
}
// New handler for the 'action' event
static void data_source_action(void *data, struct wl_data_source *source, uint32_t dnd_action) {
    // We don't need to do anything specific here for a simple file drag,
    // but this function must exist to prevent the crash.
}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_target,
    .send = data_source_send,
    .cancelled = data_source_cancelled,
    .dnd_drop_performed = data_source_dnd_drop_performed,
    .dnd_finished = data_source_dnd_finished,
    .action = data_source_action, 
};


static void pointer_enter(void *data, struct wl_pointer *p, uint32_t s, struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y) {}
static void pointer_leave(void *data, struct wl_pointer *p, uint32_t s, struct wl_surface *surf) {}
static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {}
static void pointer_axis(void *data, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state_w) {
    struct State *state = data;
    if (state_w == WL_POINTER_BUTTON_STATE_PRESSED && button == 272) { // Left Click
        printf("Click detected. Starting Drag for %s\n", state->filename);
        
        struct wl_data_device *device = wl_data_device_manager_get_data_device(state->data_device_manager, state->seat);
        state->source = wl_data_device_manager_create_data_source(state->data_device_manager);
        
        wl_data_source_add_listener(state->source, &data_source_listener, state);
        wl_data_source_offer(state->source, "text/uri-list");
        wl_data_source_set_actions(state->source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);

        // Create the icon that follows the cursor
        state->icon_surface = wl_compositor_create_surface(state->compositor);
        draw_buffer(state, state->icon_surface, 150, 40, 0xCCDDDDDD); // Semi-transparent grey icon

        wl_data_device_start_drag(device, state->source, state->main_surface, state->icon_surface, serial);
    }
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
    .button = pointer_button, .axis = pointer_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct State *state = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !state->pointer) {
        state->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(state->pointer, &pointer_listener, state);
    }
}
static const struct wl_seat_listener seat_listener = { .capabilities = seat_capabilities };

// --- Registry & XDG Boilerplate ---
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *b, uint32_t s) { xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener xdg_wm_base_listener = { .ping = xdg_wm_base_ping };


static void registry_handle_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
    struct State *s = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        s->compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_shm_interface.name) == 0)
        s->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (strcmp(iface, wl_data_device_manager_interface.name) == 0)
        s->data_device_manager = wl_registry_bind(r, name, &wl_data_device_manager_interface, 3);
    else if (strcmp(iface, wl_seat_interface.name) == 0) {
        // CHANGED: Use version 1 to avoid unhandled 'name' events
        s->seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
        wl_seat_add_listener(s->seat, &seat_listener, s);
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        s->xdg_wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(s->xdg_wm_base, &xdg_wm_base_listener, s);
    }
}
static const struct wl_registry_listener registry_listener = { .global = registry_handle_global, .global_remove = NULL };

static void xdg_surface_configure(void *data, struct xdg_surface *s, uint32_t serial) {
    struct State *state = data;
    xdg_surface_ack_configure(s, serial);
    draw_buffer(state, state->main_surface, 200, 50, 0xFFFFFFFF); // Draw main window
}
static const struct xdg_surface_listener xdg_surface_listener = { .configure = xdg_surface_configure };
static void xdg_toplevel_close(void *data, struct xdg_toplevel *t) { ((struct State*)data)->running = 0; }
static void xdg_toplevel_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s) {}
static const struct xdg_toplevel_listener xdg_toplevel_listener = { .configure = xdg_toplevel_configure, .close = xdg_toplevel_close };

// --- Main ---
int main(int argc, char **argv) {
    if (argc < 2) { printf("Usage: %s <file_path>\n", argv[0]); return 1; }
    
    char *path = realpath(argv[1], NULL);
    if (!path) return 1;

    struct State state = {0};
    state.running = 1;
    state.filename = strrchr(path, '/');
    if (!state.filename) state.filename = path; else state.filename++;
    state.uri_content = create_uri_list(path);
    free(path);

    state.display = wl_display_connect(NULL);
    if (!state.display) return 1;

    struct wl_registry *reg = wl_display_get_registry(state.display);
    wl_registry_add_listener(reg, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    if (!state.compositor || !state.shm || !state.data_device_manager || !state.xdg_wm_base) {
        fprintf(stderr, "Missing Wayland globals.\n");
        return 1;
    }

    // Create Main Window (Source)
    state.main_surface = wl_compositor_create_surface(state.compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.main_surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);
    xdg_toplevel_set_title(state.xdg_toplevel, state.filename);
    
    wl_surface_commit(state.main_surface);

    printf("Window open. Click it to drag '%s'.\n", state.filename);

    while (state.running && wl_display_dispatch(state.display) != -1);

    free(state.uri_content);
    return 0;
}
