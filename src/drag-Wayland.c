/*
 * MIT License
 *
 * Copyright (c) 2026 Klevis Imeri
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h" 
#include "viewporter-client-protocol.h" 
#include "macros.h"
#include "shared.h"

#define BTN_LEFT 272

static int create_shm_file(off_t size) {
  int fd = memfd_create("wl-shm", MFD_CLOEXEC);
  ftruncate(fd, size);
  return fd;
}



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
  struct wl_surface *icon_surface;
  struct wl_subsurface *icon_sub;
  struct wl_surface *drag_icon_surface;
  struct wl_data_source *source;
  struct wl_cursor_theme *cursor_theme;
  struct wl_surface *cursor_surface;
  struct wl_cursor *cross_cursor;
  struct wl_buffer *shield_buffer;
  struct wl_shm_pool *shield_pool;
  int shield_fd;
  int shield_w, shield_h;
  struct wp_viewporter *viewporter;
  struct wp_viewport *shield_viewport;
  struct wl_buffer *icon_buffer;
  struct wl_shm_pool *icon_pool;
  int icon_fd;
  FileInfo* file;
  int running;
  int real_drag_active;
  struct wl_callback *frame_cb;
  int cursor_x, cursor_y;
  int pending_update; 
} State;
static void SetCrossCursor(State *st, uint32_t serial) {
  struct wl_cursor_image *image = st->cross_cursor->images[0];
  struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
  wl_pointer_set_cursor(
    st->pointer,
    serial,
    st->cursor_surface,
    image->hotspot_x,
    image->hotspot_y
  );
  wl_surface_attach(st->cursor_surface, buffer, 0, 0);
  wl_surface_damage(st->cursor_surface, 0, 0, image->width, image->height);
  wl_surface_commit(st->cursor_surface);
}
static void DestroyState(State *st) {
  if (st->shield_viewport) wp_viewport_destroy(st->shield_viewport);
  if (st->viewporter) wp_viewporter_destroy(st->viewporter);
  if (st->layer_surface) { zwlr_layer_surface_v1_destroy(st->layer_surface); }
  if (st->icon_sub) { wl_subsurface_destroy(st->icon_sub); }
  if (st->icon_surface) wl_surface_destroy(st->icon_surface);
  if (st->drag_icon_surface) wl_surface_destroy(st->drag_icon_surface);
  if (st->cursor_surface) wl_surface_destroy(st->cursor_surface);
  if (st->main_surface) wl_surface_destroy(st->main_surface);
  if (st->source) { wl_data_source_destroy(st->source); }
  if (st->cursor_theme) { wl_cursor_theme_destroy(st->cursor_theme); }
  if (st->layer_shell) zwlr_layer_shell_v1_destroy(st->layer_shell);
  if (st->compositor) wl_compositor_destroy(st->compositor);
  if (st->shm) wl_shm_destroy(st->shm);
  if (st->subcompositor) wl_subcompositor_destroy(st->subcompositor);
  if (st->ddm) wl_data_device_manager_destroy(st->ddm);
  if (st->pointer) wl_pointer_release(st->pointer);
  if (st->seat) wl_seat_destroy(st->seat);
  if (st->shield_buffer) wl_buffer_destroy(st->shield_buffer);
  if (st->shield_pool) wl_shm_pool_destroy(st->shield_pool);
  if (st->shield_fd >= 0) close(st->shield_fd);
  if (st->icon_buffer) wl_buffer_destroy(st->icon_buffer);
  if (st->icon_pool) wl_shm_pool_destroy(st->icon_pool);
  if (st->icon_fd >= 0) close(st->icon_fd);
  if (st->file->name) FileInfoFree(st->file);
  if (st->display) wl_display_disconnect(st->display);
}
struct wl_buffer* GetOrDrawIcon(State *st, const char *text) {
  if (st->icon_buffer) return st->icon_buffer;

  int w, h;
  GetTextSize(text, &w, &h);

  int stride = w * 4;
  int size = stride * h;

  st->icon_fd = create_shm_file(size);
  if (st->icon_fd < 0) return NULL;

  unsigned int *data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, st->icon_fd, 0);
  if (data == MAP_FAILED) {
    close(st->icon_fd);
    return NULL;
  }

  RenderTextToBuffer(text, data, w, h);

  munmap(data, size);

  st->icon_pool = wl_shm_create_pool(st->shm, st->icon_fd, size);
  st->icon_buffer = wl_shm_pool_create_buffer(
    st->icon_pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888
  );

  return st->icon_buffer;
}
void DrawInvisibleShield(State *st, int w, int h) {
  if (w <= 0 || h <= 0) return;
  if (st->viewporter) {
    if (!st->shield_buffer) {
      int size = 4;
      st->shield_fd = create_shm_file(size);
      uint32_t *data = mmap(
        NULL, size,
        PROT_READ|PROT_WRITE, MAP_SHARED,
        st->shield_fd, 0
      );
      if (data != MAP_FAILED) {
        *data = 0x00000000;
        munmap(data, size);
      }
      st->shield_pool = wl_shm_create_pool(st->shm, st->shield_fd, size);
      st->shield_buffer = wl_shm_pool_create_buffer(
        st->shield_pool, 0, 1, 1, 4, WL_SHM_FORMAT_ARGB8888
      );
    }

    if (!st->shield_viewport) {
      st->shield_viewport = wp_viewporter_get_viewport(st->viewporter, st->main_surface);
    }

    wp_viewport_set_destination(st->shield_viewport, w, h);    
    wl_surface_attach(st->main_surface, st->shield_buffer, 0, 0);    
    wl_surface_damage(st->main_surface, 0, 0, w, h);
    wl_surface_commit(st->main_surface);

    return;
  }

  if (st->shield_buffer && st->shield_w == w && st->shield_h == h) return;

  if (st->shield_buffer) wl_buffer_destroy(st->shield_buffer);
  if (st->shield_pool) wl_shm_pool_destroy(st->shield_pool);
  if (st->shield_fd >= 0) close(st->shield_fd);

  st->shield_w = w;
  st->shield_h = h;

  int stride = w * 4;
  int size = stride * h;
  st->shield_fd = create_shm_file(size);  

  st->shield_pool = wl_shm_create_pool(st->shm, st->shield_fd, size);
  st->shield_buffer = wl_shm_pool_create_buffer(
    st->shield_pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888
  );

  wl_surface_attach(st->main_surface, st->shield_buffer, 0, 0);
  wl_surface_damage(st->main_surface, 0, 0, w, h);
  wl_surface_commit(st->main_surface);
}

static void schedule_icon_update(State *st);
static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
  (void)time;
  State *st = data;
  wl_callback_destroy(cb);
  st->frame_cb = NULL;

  if (st->pending_update) {
    schedule_icon_update(st);
  }
}
static const struct wl_callback_listener frame_listener = { .done = frame_done };
static void schedule_icon_update(State *st) {
  if (st->frame_cb) {
    st->pending_update = 1;
    return;
  }

  if (st->icon_sub && !st->real_drag_active) {
    wl_subsurface_set_position(
      st->icon_sub,
      st->cursor_x + 15,
      st->cursor_y + 15
    );
    wl_surface_commit(st->main_surface);
  }

  st->pending_update = 0;

  st->frame_cb = wl_surface_frame(st->main_surface);
  wl_callback_add_listener(st->frame_cb, &frame_listener, st);
}




static void ds_drop_performed(void *data, struct wl_data_source *s) {
    (void)data, (void)s;
}
static void ds_target(void *data, struct wl_data_source *s, const char *mime_type) {
  (void)data, (void)s, (void)mime_type;
}
static void ds_send(void *d, struct wl_data_source *s, const char *m, int32_t fd) {
  (void)s;
  State *st = d;
  if (strcmp(m, "text/uri-list") == 0) { write(fd, st->file->uri, strlen(st->file->uri));}
  close(fd);
}
static void ds_cancelled(void *d, struct wl_data_source *s) {
  (void)s;
  ((State*)d)->running = 0; 
}
static void ds_finished(void *d, struct wl_data_source *s) {
  (void)s;
  ((State*)d)->running = 0;
}
static void ds_action(void *d, struct wl_data_source *s, uint32_t a) {
  (void)d, (void)s, (void)a; 
}
static const struct wl_data_source_listener ds_listener = {
  .target = ds_target,
  .send = ds_send,
  .cancelled = ds_cancelled,
  .dnd_drop_performed = ds_drop_performed,
  .dnd_finished = ds_finished,
  .action = ds_action
};





static void pointer_enter(
  void *d,
  struct wl_pointer *p,
  uint32_t s,
  struct wl_surface *surf,
  wl_fixed_t x,
  wl_fixed_t y
) {
  (void)p, (void)s, (void)surf;
  State *st = d;
  if (!st->real_drag_active && st->icon_sub) {
    wl_subsurface_set_position(
      st->icon_sub,
      wl_fixed_to_int(x) + 15,
      wl_fixed_to_int(y) + 15
    );
    wl_surface_commit(st->main_surface);
  }
}
static void pointer_leave(
  void *data,
  struct wl_pointer *p,
  uint32_t s,
  struct wl_surface *surf
) {(void)data, (void)p, (void)s, (void)surf;}
static void pointer_motion(
  void *data,
  struct wl_pointer *p,
  uint32_t time,
  wl_fixed_t x,
  wl_fixed_t y
) {
  State *st = data;
  (void)p, (void)time;
  if (!st->real_drag_active && st->icon_sub) {
    wl_subsurface_set_position(
      st->icon_sub,
      wl_fixed_to_int(x) + 15,
      wl_fixed_to_int(y) + 15
    );
    wl_surface_commit(st->main_surface);
  }
}
static void pointer_button(
  void *data,
  struct wl_pointer *p,
  uint32_t serial,
  uint32_t time,
  uint32_t button,
  uint32_t state_w
) {
  State *st = data;
  (void)p, (void)time;
  if (
    state_w == WL_POINTER_BUTTON_STATE_PRESSED && 
    button == BTN_LEFT && 
    !st->real_drag_active
  ) {
    st->real_drag_active = 2;
    SetCrossCursor(st, serial);

    struct wl_data_device *device = wl_data_device_manager_get_data_device(st->ddm, st->seat);
    st->source = wl_data_device_manager_create_data_source(st->ddm);
    wl_data_source_add_listener(st->source, &ds_listener, st);
    wl_data_source_offer(st->source, "text/uri-list");
    wl_data_source_set_actions(
      st->source,
      WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE
    );

    wl_data_device_start_drag(
      device,
      st->source,
      st->main_surface,
      st->drag_icon_surface,
      serial
    );

    struct wl_region *region = wl_compositor_create_region(st->compositor);
    wl_surface_set_input_region(st->main_surface, region); 
    wl_region_destroy(region);
    wl_surface_commit(st->main_surface);

    if (st->icon_sub) {
      wl_subsurface_set_position(st->icon_sub, -3000, -3000);
      wl_surface_commit(st->main_surface);
    }
  } else if (state_w == WL_POINTER_BUTTON_STATE_RELEASED && button == BTN_LEFT) {
    st->real_drag_active = 0;
  }
}
static void pointer_axis(
  void *data,
  struct wl_pointer *p,
  uint32_t time,
  uint32_t axis,
  wl_fixed_t value
) {(void)data, (void)p, (void)time, (void)axis, (void)value;}
static const struct wl_pointer_listener pointer_listener = {
  .enter = pointer_enter,
  .leave = pointer_leave,
  .motion = pointer_motion,
  .button = pointer_button,
  .axis = pointer_axis
};




static void seat_name(void *data, struct wl_seat *seat, const char *name) {
  (void)data; (void)seat; (void)name;
}
static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps) {
  State *st = data;
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !st->pointer) {
    st->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(st->pointer, &pointer_listener, st);
  }
}
static const struct wl_seat_listener seat_listener = {
  .capabilities = seat_caps,
  .name = seat_name 
};






static void layer_surf_configure(
  void *data,
  struct zwlr_layer_surface_v1 *surface,
  uint32_t serial,
  uint32_t w,
  uint32_t h
) {
  State *st = data;
  zwlr_layer_surface_v1_ack_configure(surface, serial);
  if (w == 0 || h == 0) return;

  if (!st->real_drag_active && st->main_surface) {
    DrawInvisibleShield(st, w, h);
    struct wl_region *region = wl_compositor_create_region(st->compositor);
    wl_surface_set_input_region(st->main_surface, NULL);
    wl_region_destroy(region);
    wl_surface_commit(st->main_surface);
  }
}
static void layer_surf_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
  State *st = data;
  (void)surface;
  st->running = 0;
}
static const struct zwlr_layer_surface_v1_listener layer_surf_listener = {
  .configure = layer_surf_configure,
  .closed = layer_surf_closed
};






static void handle_global(
  void *data,
  struct wl_registry *r,
  uint32_t name,
  const char *iface,
  uint32_t ver
) {
  State *s = data;
  (void) ver;
  if (!strcmp(iface, wl_compositor_interface.name)) 
    s->compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
  else if (!strcmp(iface, wl_subcompositor_interface.name)) 
    s->subcompositor = wl_registry_bind(r, name, &wl_subcompositor_interface, 1);
  else if (!strcmp(iface, wl_shm_interface.name)) 
    s->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
  else if (!strcmp(iface, wl_data_device_manager_interface.name)) 
    s->ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, 3);
  else if (!strcmp(iface, wl_seat_interface.name)) {
    struct wl_seat *seat = wl_registry_bind(r, name, &wl_seat_interface, 3);
    s->seat = seat; 
    wl_seat_add_listener(seat, &seat_listener, s);
  } else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
    s->layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
  } else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
     s->viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
  }
}
static const struct wl_registry_listener reg_listener = {
  .global = handle_global,
  .global_remove = NULL
};




int main(int argc, char **argv) {
  State state = {
    .running = 1,
    .shield_fd = -1,
    .icon_fd = -1
  };

  defer { DestroyState(&state); };

  state.file = CommandLineArguments(argc, argv);
  if(!state.file) return 1;

  state.display = wl_display_connect(NULL);
  if (!state.display) return 1;

  struct wl_registry *reg = wl_display_get_registry(state.display);
  wl_registry_add_listener(reg, &reg_listener, &state);
  wl_display_roundtrip(state.display);

  if (!state.compositor || !state.layer_shell || !state.ddm || !state.seat) {
    LOG("Missing required Wayland globals.\n");
    return 1;
  }
  
  state.cursor_theme = wl_cursor_theme_load(NULL, 24, state.shm);
  state.cross_cursor = wl_cursor_theme_get_cursor(state.cursor_theme, "crosshair");
  state.cursor_surface = wl_compositor_create_surface(state.compositor);

  struct wl_buffer *icon_buf = GetOrDrawIcon(&state, state.file->name);
  if (!icon_buf) return 1;

  state.drag_icon_surface = wl_compositor_create_surface(state.compositor);
  wl_surface_attach(state.drag_icon_surface, icon_buf, 0, 0);
  wl_surface_commit(state.drag_icon_surface);

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

  state.icon_surface = wl_compositor_create_surface(state.compositor);
  state.icon_sub = wl_subcompositor_get_subsurface(
    state.subcompositor, state.icon_surface, state.main_surface
  );
  wl_subsurface_set_position(state.icon_sub, -200, -200);

  wl_subsurface_set_desync(state.icon_sub);

  wl_surface_attach(state.icon_surface, icon_buf, 0, 0); 
  wl_surface_commit(state.icon_surface);

  wl_surface_commit(state.main_surface);

  while (state.running && wl_display_dispatch(state.display) != -1);

  return 0;
}
