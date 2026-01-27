#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <linux/input-event-codes.h>

/* memfd_create is not available in all libc versions */
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef SYS_memfd_create
#define SYS_memfd_create 319
#endif

static inline int memfd_create(const char *name, unsigned int flags) {
  return syscall(SYS_memfd_create, name, flags);
}

#define __DEFER__(func_name, var_name) \
  auto void func_name(int*); \
  int var_name __attribute__((__cleanup__(func_name))); \
  auto void func_name(int*)

#define DEFER_ONE(N) __DEFER__(__DEFER__FUNC ## N, __DEFER__VAR ## N)
#define DEFER_TWO(N) DEFER_ONE(N)
#define defer DEFER_TWO(__COUNTER__)

#ifdef DEBUG
#define LOG(...) fprintf(stderr, "[DEBUG] " __VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif

typedef struct {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct wl_seat *seat;
  struct wl_pointer *pointer;
  struct wl_data_device_manager *data_device_manager;
  struct wl_data_device *data_device;
  struct xdg_wm_base *xdg_wm_base;
  struct wl_output *output;
} WaylandGlobals;

typedef struct {
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *toplevel;
  struct wl_buffer *buffer;
  int width;
  int height;
  uint32_t *shm_data;
  int needs_redraw;
} Window;

typedef struct {
  WaylandGlobals globals;
  Window window;
  struct wl_data_source *data_source;
  const char *uri_data;
  int dragging;
  int last_x;
  int last_y;
} AppState;

char* create_uri_list(const char *path) {
  size_t len = strlen(path);
  char *output = malloc(len * 3 + 16);
  if (!output) return NULL;

  char *p = output;
  p += sprintf(p, "file://");
  if (path[0] != '/') *p++ = '/';

  for (const char *s = path; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
        c == '~' || c == '/') {
      *p++ = c;
    } else {
      p += sprintf(p, "%%%02X", c);
    }
  }
  *p++ = '\r';
  *p++ = '\n';
  *p = '\0';
  return output;
}

static void data_source_target(void *data, struct wl_data_source *source,
                               const char *mime_type) {
  (void)data; (void)source; (void)mime_type;
  LOG("Target MIME type: %s\n", mime_type ? mime_type : "none");
}

static void data_source_send(void *data, struct wl_data_source *source,
                             const char *mime_type, int32_t fd) {
  (void)source;
  AppState *state = (AppState *)data;
  LOG("Send request for MIME: %s\n", mime_type);

  if (strcmp(mime_type, "text/uri-list") == 0) {
    ssize_t len = strlen(state->uri_data);
    ssize_t written = write(fd, state->uri_data, len);
    if (written < len) {
      LOG("Warning: write incomplete (%ld/%ld)\n", written, len);
    }
  }
  close(fd);
}

static void data_source_action(void *data, struct wl_data_source *source,
                               uint32_t dnd_action) {
  (void)data; (void)source; (void)dnd_action;
  LOG("DnD action: %u\n", dnd_action);
}

static void __attribute__((unused)) data_source_finish(void *data, struct wl_data_source *source) {
  (void)source;
  AppState *state = (AppState *)data;
  LOG("Drop finished\n");
  state->dragging = 0;
}

static void data_source_cancelled(void *data, struct wl_data_source *source) {
  (void)source;
  AppState *state = (AppState *)data;
  LOG("DnD cancelled\n");
  state->dragging = 0;
}

static const struct wl_data_source_listener data_source_listener = {
  .target = data_source_target,
  .send = data_source_send,
  .action = data_source_action,
  .cancelled = data_source_cancelled,
};

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
  (void)data; (void)pointer; (void)serial; (void)surface; (void)sx; (void)sy;
  LOG("Pointer enter\n");
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
  (void)data; (void)pointer; (void)serial; (void)surface;
  LOG("Pointer leave\n");
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy) {
  (void)pointer; (void)time;
  AppState *state = (AppState *)data;
  int x = wl_fixed_to_int(sx);
  int y = wl_fixed_to_int(sy);

  if (x != state->last_x || y != state->last_y) {
    state->last_x = x;
    state->last_y = y;
    state->window.needs_redraw = 1;
  }
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state_val) {
  (void)pointer; (void)time;
  AppState *app_state = (AppState *)data;
  LOG("Button %u: %s\n", button, state_val == WL_POINTER_BUTTON_STATE_PRESSED ? "pressed" : "released");

  if (state_val == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
    struct wl_data_source *source = wl_data_device_manager_create_data_source(
        app_state->globals.data_device_manager);
    if (!source) {
      fprintf(stderr, "Failed to create data source\n");
      return;
    }

    wl_data_source_add_listener(source, &data_source_listener, app_state);
    wl_data_source_offer(source, "text/uri-list");

    app_state->data_source = source;
    app_state->dragging = 1;

    wl_data_device_start_drag(app_state->globals.data_device, source,
                              app_state->window.surface, NULL, serial);
    LOG("Drag started\n");
  }
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
  (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener pointer_listener __attribute__((unused)) = {
  .enter = pointer_enter,
  .leave = pointer_leave,
  .motion = pointer_motion,
  .button = pointer_button,
  .axis = pointer_axis,
};


static void registry_handle_global(void *data, struct wl_registry *registry,
  uint32_t name, const char *interface, uint32_t version) {
  (void)version; (void)registry; (void)name; (void)data;
  WaylandGlobals *globals = (WaylandGlobals *)data;

  if (strcmp(interface, "wl_compositor") == 0) {
    globals->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
  } else if (strcmp(interface, "wl_shm") == 0) {
    globals->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, "wl_seat") == 0) {
    globals->seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
  } else if (strcmp(interface, "wl_data_device_manager") == 0) {
    globals->data_device_manager = wl_registry_bind(
      registry, name, &wl_data_device_manager_interface, 3);
  } else if (strcmp(interface, "xdg_wm_base") == 0) {
    globals->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
  } else if (strcmp(interface, "wl_output") == 0 && !globals->output) {
    globals->output = wl_registry_bind(registry, name, &wl_output_interface, 2);
  }
}


static void registry_handle_global_remove(void *data, struct wl_registry *registry,
  uint32_t name) {
  (void)data; (void)registry; (void)name;
  LOG("Registry remove: %u\n", name);
}

static const struct wl_registry_listener registry_listener __attribute__((unused)) = {
  .global = registry_handle_global,
  .global_remove = registry_handle_global_remove,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
  uint32_t serial) {
  (void)data;
  xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener __attribute__((unused)) = {
  .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
  (void)data;
  xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener __attribute__((unused)) = {
  .configure = xdg_surface_configure,
};

struct wl_shm_pool* create_shm_pool(struct wl_shm *shm, int size, void **data_out) {
  int fd = memfd_create("wayland-pool", MFD_CLOEXEC);
  if (fd < 0) {
    perror("memfd_create");
    return NULL;
  }

  if (ftruncate(fd, size) < 0) {
    perror("ftruncate");
    close(fd);
    return NULL;
  }

  void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return NULL;
  }

  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
  close(fd);

  *data_out = data;
  return pool;
}

void redraw_window(Window *window) {
  if (!window->shm_data || !window->buffer) return;

  for (int i = 0; i < window->width * window->height; i++) {
    window->shm_data[i] = 0xFFFFFFFF;
  }

  for (int x = 0; x < window->width; x++) {
    window->shm_data[x] = 0xFF000000;
    window->shm_data[(window->height - 1) * window->width + x] = 0xFF000000;
  }
  for (int y = 0; y < window->height; y++) {
    window->shm_data[y * window->width] = 0xFF000000;
    window->shm_data[y * window->width + window->width - 1] = 0xFF000000;
  }

  wl_surface_attach(window->surface, window->buffer, 0, 0);
  wl_surface_damage_buffer(window->surface, 0, 0, window->width, window->height);
  wl_surface_commit(window->surface);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
    return 1;
  }

  AppState state = {0};
  state.uri_data = create_uri_list(argv[1]);
  if (!state.uri_data) {
    fprintf(stderr, "Failed to create URI list\n");
    return 1;
  }

  state.globals.display = wl_display_connect(NULL);
  if (!state.globals.display) {
    fprintf(stderr, "Failed to connect to Wayland display\n");
    return 1;
  }

  state.globals.registry = wl_display_get_registry(state.globals.display);
  wl_registry_add_listener(state.globals.registry, &registry_listener, &state.globals);
  wl_display_roundtrip(state.globals.display);

  if (!state.globals.compositor) {
    fprintf(stderr, "Compositor not available\n");
    return 1;
  }

  state.window.surface = wl_compositor_create_surface(state.globals.compositor);
  state.window.xdg_surface = xdg_wm_base_get_xdg_surface(state.globals.xdg_wm_base, state.window.surface);
  xdg_surface_add_listener(state.window.xdg_surface, &xdg_surface_listener, &state);
  state.window.toplevel = xdg_surface_get_toplevel(state.window.xdg_surface);
  xdg_wm_base_add_listener(state.globals.xdg_wm_base, &xdg_wm_base_listener, &state);

  state.window.width = 400;
  state.window.height = 300;

  int pool_size = state.window.width * state.window.height * 4;
  struct wl_shm_pool *pool = create_shm_pool(state.globals.shm, pool_size, (void **)&state.window.shm_data);
  if (!pool) {
    fprintf(stderr, "Failed to create shm pool\n");
    return 1;
  }

  state.window.buffer = wl_shm_pool_create_buffer(pool, 0, state.window.width, state.window.height,
                                                   state.window.width * 4, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);

  if (state.globals.seat) {
    state.globals.pointer = wl_seat_get_pointer(state.globals.seat);
    wl_pointer_add_listener(state.globals.pointer, &pointer_listener, &state);

    state.globals.data_device = wl_data_device_manager_get_data_device(
        state.globals.data_device_manager, state.globals.seat);
  }

  /* Attach buffer first, then commit to trigger configure */
  wl_surface_attach(state.window.surface, state.window.buffer, 0, 0);
  wl_surface_damage_buffer(state.window.surface, 0, 0, state.window.width, state.window.height);
  wl_surface_commit(state.window.surface);
  
  /* Now wait for configure event */
  wl_display_roundtrip(state.globals.display);

  while (wl_display_dispatch(state.globals.display) != -1) {
    // Event loop
  }

  return 0;
}
