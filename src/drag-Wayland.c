#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h" 
#include <wayland-cursor.h>
#define FONT8x16_IMPLEMENTATION
#include "font8x16.h"

// --- Helpers ---
static int create_shm_file(off_t size) {
    int fd = memfd_create("wl-shm", MFD_CLOEXEC);
    ftruncate(fd, size);
    return fd;
}


// --- Draw the Icon ---
void draw_icon(struct wl_shm *shm, struct wl_surface *surface, const char *text) {
    int len = strlen(text);
    
    // Font dimensions for 8x16
    const int char_w = 8;
    const int char_h = 16;
    const int padding_x = 12;
    const int padding_y = 8;

    int w = (len * char_w) + (padding_x * 2);
    int h = char_h + (padding_y * 2);
    int stride = w * 4;
    int size = stride * h;
    
    int fd = create_shm_file(size);
    if (fd < 0) return;
    uint32_t *data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    
    // Styling: Dark background bubble
    uint32_t color_bg   = 0xFF222222; 
    uint32_t color_text = 0xFFFFFFFF; 

    for (int i = 0; i < w * h; i++) data[i] = color_bg;
    
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        
        for (int r = 0; r < 16; r++) { // 16 rows high
            for (int col = 0; col < 8; col++) { // 8 bits wide
                
                // Bit check: In this font, 0x80 (1000 0000) is the leftmost pixel.
                // We shift it right for each column.
                if (font8x16[c][r] & (0x80 >> col)) {
                    int x = padding_x + (i * char_w) + col;
                    int y = padding_y + r;
                    data[y * w + x] = color_text;
                }
            }
        }
    }
    
    munmap(data, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage(surface, 0, 0, w, h);
    wl_surface_commit(surface);
    
    wl_buffer_destroy(buf);
    wl_shm_pool_destroy(pool);
    close(fd);
}

// --- Draw Invisible Shield ---
void draw_invisible_shield(struct wl_compositor *comp, struct wl_shm *shm, struct wl_surface *surface, int w, int h) {
    int stride = w * 4;
    int size = stride * h;
    int fd = create_shm_file(size);
    
    void *data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    memset(data, 0, size); // Zero = Transparent
    munmap(data, size);
    
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage(surface, 0, 0, w, h);
    
    // Transparent region logic handled in main configuration now
    // But we still need a buffer to make the surface exist
    wl_surface_commit(surface);
    wl_buffer_destroy(buf);
    wl_shm_pool_destroy(pool);
    close(fd);
}

// --- State ---
typedef struct {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wl_seat *seat;            
    struct wl_pointer *pointer;      
    struct wl_data_device_manager *ddm;
    struct zwlr_layer_shell_v1 *layer_shell;
    
    struct wl_surface *main_surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    
    struct wl_surface *icon_surface; // The subsurface icon (follows mouse)
    struct wl_subsurface *icon_sub; 
    
    struct wl_surface *drag_icon_surface; // The actual DND icon (pre-rendered)
    
    struct wl_data_source *source;

    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    struct wl_cursor *cross_cursor;
    
    char *uri;
    char *filename;
    int running;
    int real_drag_active;
} State;

static void set_cross_cursor(State *st, uint32_t serial) {
    struct wl_cursor_image *image = st->cross_cursor->images[0];
    struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
    wl_pointer_set_cursor(st->pointer, serial, st->cursor_surface, image->hotspot_x, image->hotspot_y);
    wl_surface_attach(st->cursor_surface, buffer, 0, 0);
    wl_surface_damage(st->cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(st->cursor_surface);
}

// --- Drag Logic ---

static void cleanup_and_exit(State *st) {
    // If we have a layer surface, destroy it now
    if (st->layer_surface) {
        zwlr_layer_surface_v1_destroy(st->layer_surface);
        st->layer_surface = NULL;
    }
    st->running = 0;
}

static void ds_drop_performed(void *data, struct wl_data_source *s) {
    // Wait for finished
}
static void ds_target(void *data, struct wl_data_source *s, const char *mime_type) {}
static void ds_send(void *d, struct wl_data_source *s, const char *m, int32_t fd) {
    State *st = d;
    if (strcmp(m, "text/uri-list") == 0) {
        write(fd, st->uri, strlen(st->uri));
    }
    close(fd);
}
static void ds_cancelled(void *d, struct wl_data_source *s) { cleanup_and_exit((State*)d); }
static void ds_finished(void *d, struct wl_data_source *s) { cleanup_and_exit((State*)d); }
static void ds_action(void *d, struct wl_data_source *s, uint32_t a) {}

static const struct wl_data_source_listener ds_listener = {
    .target = ds_target,
    .send = ds_send,
    .cancelled = ds_cancelled,
    .dnd_drop_performed = ds_drop_performed,
    .dnd_finished = ds_finished,
    .action = ds_action
};

// --- Input Logic ---

static void pointer_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y) {
    State *st = d;
    if (!st->real_drag_active && st->icon_sub) {
        wl_subsurface_set_position(st->icon_sub, wl_fixed_to_int(x) + 15, wl_fixed_to_int(y) + 15);
        wl_surface_commit(st->main_surface);
    }
}
static void pointer_leave(void *data, struct wl_pointer *p, uint32_t s, struct wl_surface *surf) {}
static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    State *st = data;
    if (!st->real_drag_active && st->icon_sub) {
        wl_subsurface_set_position(st->icon_sub, wl_fixed_to_int(x) + 15, wl_fixed_to_int(y) + 15);
        wl_surface_commit(st->main_surface);
    }
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state_w) {
    State *st = data;

    // Left Click (272) - Button Pressed
    if (state_w == WL_POINTER_BUTTON_STATE_PRESSED && button == 272 && !st->real_drag_active) {
        st->real_drag_active = 1;
        set_cross_cursor(st, serial);

        // 1. Create Data Source
        struct wl_data_device *device = wl_data_device_manager_get_data_device(st->ddm, st->seat);
        st->source = wl_data_device_manager_create_data_source(st->ddm);
        wl_data_source_add_listener(st->source, &ds_listener, st);
        wl_data_source_offer(st->source, "text/uri-list");
        wl_data_source_set_actions(st->source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);

        // 2. Start Drag INSTANTLY using PRE-RENDERED surface
        wl_data_device_start_drag(device, st->source, st->main_surface, st->drag_icon_surface, serial);

        // 3. Make the shield effectively gone immediately (click-through)
        struct wl_region *region = wl_compositor_create_region(st->compositor);
        wl_surface_set_input_region(st->main_surface, region); // Empty region
        wl_region_destroy(region);
        wl_surface_commit(st->main_surface);

        // 4. Hide local subsurface
        if (st->icon_sub) {
            wl_subsurface_set_position(st->icon_sub, -3000, -3000);
            wl_surface_commit(st->main_surface);
        }

        // wl_display_flush(st->display); 

        // usleep(100000);

    } else if (state_w == WL_POINTER_BUTTON_STATE_RELEASED && button == 272) {
        st->real_drag_active = 0;
        // Do nothing. Wait for DND events.
    }
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t value) {}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis
};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps) {
    State *st = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !st->pointer) {
        st->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(st->pointer, &pointer_listener, st);
    }
}
static const struct wl_seat_listener seat_listener = { .capabilities = seat_caps };

// --- Layer Shell Listener ---
static void layer_surf_configure(void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t w, uint32_t h) {
    State *st = data;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    
    if (!st->real_drag_active && st->main_surface) {
        int width = (w == 0) ? 3000 : w;
        int height = (h == 0) ? 3000 : h;
        draw_invisible_shield(st->compositor, st->shm, st->main_surface, width, height);
        
        // Ensure input region covers everything initially
        struct wl_region *region = wl_compositor_create_region(st->compositor);
        wl_region_add(region, 0, 0, width, height);
        wl_surface_set_input_region(st->main_surface, region);
        wl_region_destroy(region);
        wl_surface_commit(st->main_surface);
    }
}
static void layer_surf_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    State *st = data;
    st->running = 0;
}
static const struct zwlr_layer_surface_v1_listener layer_surf_listener = {
    .configure = layer_surf_configure,
    .closed = layer_surf_closed
};

// --- Registry ---
static void handle_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
    State *s = data;
    if (!strcmp(iface, wl_compositor_interface.name)) 
        s->compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_subcompositor_interface.name)) 
        s->subcompositor = wl_registry_bind(r, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(iface, wl_shm_interface.name)) 
        s->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, wl_data_device_manager_interface.name)) 
        s->ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, 3);
    else if (!strcmp(iface, wl_seat_interface.name)) {
        struct wl_seat *seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
        s->seat = seat; 
        wl_seat_add_listener(seat, &seat_listener, s);
    }
    else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
        s->layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
    }
}
static const struct wl_registry_listener reg_listener = { .global = handle_global, .global_remove = NULL };

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file-path>\n", argv[0]);
        return 1;
    }

    char *path = realpath(argv[1], NULL);
    if (!path) {
        perror("realpath");
        return 1;
    }

    State state = {0};
    state.running = 1;
    state.uri = malloc(strlen(path)*3 + 16);
    sprintf(state.uri, "file://%s\r\n", path);
    
    state.filename = strrchr(path, '/');
    if (!state.filename) state.filename = path; else state.filename++;
    
    state.display = wl_display_connect(NULL);
    if (!state.display) return 1;
    
    struct wl_registry *reg = wl_display_get_registry(state.display);
    wl_registry_add_listener(reg, &reg_listener, &state);
    wl_display_roundtrip(state.display);

    if (!state.compositor || !state.layer_shell || !state.ddm || !state.seat) {
        fprintf(stderr, "Missing required Wayland globals.\n");
        return 1;
    }
    state.cursor_theme = wl_cursor_theme_load(NULL, 24, state.shm);
    state.cross_cursor = wl_cursor_theme_get_cursor(state.cursor_theme, "crosshair");
    state.cursor_surface = wl_compositor_create_surface(state.compositor);

    // --- PRE-RENDER DRAG ICON ---
    // Do this BEFORE the main loop to ensure zero latency on click
    state.drag_icon_surface = wl_compositor_create_surface(state.compositor);
    draw_icon(state.shm, state.drag_icon_surface, state.filename);
    // ----------------------------

    state.main_surface = wl_compositor_create_surface(state.compositor);
    
    state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        state.layer_shell, state.main_surface, NULL, 
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "drag-overlay"
    );
    
    zwlr_layer_surface_v1_set_size(state.layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(state.layer_surface, 15); 
    zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, 0);
    
    zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surf_listener, &state);
    
    wl_surface_commit(state.main_surface);
    wl_display_roundtrip(state.display);

    // Setup local moving icon (subsurface)
    state.icon_surface = wl_compositor_create_surface(state.compositor);
    state.icon_sub = wl_subcompositor_get_subsurface(state.subcompositor, state.icon_surface, state.main_surface);
    wl_subsurface_set_desync(state.icon_sub);
    wl_subsurface_set_position(state.icon_sub, -200, -200);
    draw_icon(state.shm, state.icon_surface, state.filename);
    
    wl_surface_commit(state.main_surface);

    while (state.running && wl_display_dispatch(state.display) != -1);

    free(path);
    free(state.uri);
    return 0;
}
