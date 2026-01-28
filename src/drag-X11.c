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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>


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
  Atom Aware,
  Selection,
  Enter,
  Position,
  DndStatus,
  Leave,
  Drop,
  Finished,
  ActionCopy,
  UriList,
  Targets;
} Atoms;

typedef struct {
  Display *d;
  Window root;
  Window src_window;
  Atoms atoms;
  int version;
} DndContext;


char* atom_name(Display *d, Atom a) {
  char *name = XGetAtomName(d, a);
  return name ? name : "UNKNOWN";
}

// RFC 3986
// Turns '/home/user/My File.txt' into 'file:///home/user/My%20File.txt\r\n'
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
        (c >= '0' && c <= '9') || c == '-' || c == '.' || 
        c == '_' || c == '~' || c == '/') {
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

void init_atoms(Display *d, Atoms *a) {
  a->Aware       = XInternAtom(d, "XdndAware", False);
  a->Selection   = XInternAtom(d, "XdndSelection", False);
  a->Enter       = XInternAtom(d, "XdndEnter", False);
  a->Position    = XInternAtom(d, "XdndPosition", False);
  a->DndStatus   = XInternAtom(d, "XdndStatus", False); 
  a->Leave       = XInternAtom(d, "XdndLeave", False);
  a->Drop        = XInternAtom(d, "XdndDrop", False);
  a->Finished    = XInternAtom(d, "XdndFinished", False);
  a->ActionCopy  = XInternAtom(d, "XdndActionCopy", False);
  a->UriList     = XInternAtom(d, "text/uri-list", False);
  a->Targets     = XInternAtom(d, "TARGETS", False);
}

void send_msg(
  DndContext *ctx, Window target, Atom type,
  long d0, long d1, long d2, long d3, long d4
) {
  LOG("Sending %s to Window 0x%lx\n", atom_name(ctx->d, type), target);
  XClientMessageEvent m = {
    .type = ClientMessage,
    .display = ctx->d,
    .window = target,
    .message_type = type,
    .format = 32
  };
  m.data.l[0] = d0; m.data.l[1] = d1; m.data.l[2] = d2; 
  m.data.l[3] = d3; m.data.l[4] = d4;
  XSendEvent(ctx->d, target, False, NoEventMask, (XEvent*)&m);
  XFlush(ctx->d);
}


Window find_deepest_child(Display *d, Window root, int x, int y, Window ignore) {
  Window current = root;
  int dest_x, dest_y;
  while (1) {
    Window child;
    XTranslateCoordinates(d, root, current, x, y, &dest_x, &dest_y, &child);
    if (child == None || child == ignore) return current;
    current = child;
  }
}

Window find_xdnd_target(DndContext *ctx, int x, int y) {
  Window target = find_deepest_child(ctx->d, ctx->root, x, y, ctx->src_window);

  while (target) {
    Atom type; int fmt; unsigned long n, b; unsigned char *prop = NULL;
    if (XGetWindowProperty(
      ctx->d, target, ctx->atoms.Aware, 0, 4, False, AnyPropertyType,
      &type, &fmt, &n, &b, &prop
    ) == Success) {
      if (type != None) {
        if (prop) XFree(prop);
        LOG("Found XdndAware Target: 0x%lx\n", target);
        return target;
      }
      if (prop) XFree(prop);
    }

    Window root_ret, parent, *kids; unsigned int n_kids;
    if (!XQueryTree(ctx->d, target, &root_ret, &parent, &kids, &n_kids)) break;
    if (kids) XFree(kids);
    if (parent == root_ret || parent == 0) break;
    target = parent;
  }

  return 0;
}


int XSafeErrorHandler(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: %s <file_path>\n", argv[0]);
    return 1;
  }

  char *path = realpath(argv[1], NULL);
  defer { free(path); };
  if (!path) {
    LOG("Error resolving path");
    return 1;
  }

  char *uri = create_uri_list(path);
  defer { free(uri); };
  if (!uri) {
    LOG("Error creating uri from path");
    return 1;
  }

  char *display_name = strrchr(path, '/');
  if (display_name) display_name++; 
  else display_name = path;

  printf("Dragging: %s", uri); 

  Display *d = XOpenDisplay(NULL);
  if (!d) {
    LOG("Cannot open display\n");
    return 1;
  }
  defer { XCloseDisplay(d); };

  XSetErrorHandler(XSafeErrorHandler);

  DndContext ctx = {0};
  ctx.d = d;
  ctx.root = DefaultRootWindow(d);
  ctx.version = 5; 
  init_atoms(d, &ctx.atoms);

  XFontStruct *font = XLoadQueryFont(d, "fixed");
  if (!font) font = XLoadQueryFont(d, "9x15");
  defer { if(font) XFreeFont(d, font); };

  int text_w = XTextWidth(font, display_name, strlen(display_name));
  int text_h = font->ascent + font->descent;
  int pad = 4;
  int win_w = text_w + (pad * 2);
  int win_h = text_h + (pad * 2);

  ctx.src_window = XCreateSimpleWindow(
    d, ctx.root,
    0, 0, win_w, win_h,
    1, BlackPixel(d, 0), WhitePixel(d, 0)
  );
  XSetWindowAttributes attr;
  attr.override_redirect = True;
  XChangeWindowAttributes(d, ctx.src_window, CWOverrideRedirect, &attr);
  XSelectInput(d, ctx.src_window, StructureNotifyMask | ExposureMask);
  XMapWindow(d, ctx.src_window);

  GC gc = XCreateGC(d, ctx.src_window, 0, NULL);
  XSetForeground(d, gc, BlackPixel(d, 0));
  XSetFont(d, gc, font->fid);
  defer { XFreeGC(d, gc); };

  Cursor cursor = XCreateFontCursor(d, XC_cross);
  defer { XFreeCursor(d, cursor); };

  XEvent e;
  while (1) { XMaskEvent(d, StructureNotifyMask, &e); if (e.type == MapNotify) break; }


  if (XGrabPointer(
    d, ctx.src_window, False,
    PointerMotionMask | ButtonReleaseMask,
    GrabModeAsync, GrabModeAsync,
    None, cursor, CurrentTime
  ) != GrabSuccess) {
    fprintf(stderr, "Failed to grab pointer. Is another app grabbing it?\n");
    return 1;
  }

  XSetSelectionOwner(d, ctx.atoms.Selection, ctx.src_window, CurrentTime);

  Window current_target = 0;
  int dragging = 1;

  LOG("Drag started. Move mouse to target.\n");

  while (dragging) {
    XNextEvent(d, &e);

    switch (e.type) {
      case MotionNotify: {
      // [OPTIMIZATION] Event Compression
        while (XPending(d) > 0) {
          XEvent next_e;
          XPeekEvent(d, &next_e);
          if (next_e.type == MotionNotify) {
            XNextEvent(d, &e); 
          } else {
            break;
          }
        }
    
        XMoveWindow(d, ctx.src_window, e.xmotion.x_root + 15, e.xmotion.y_root + 20);
        Window new_target = find_xdnd_target(&ctx, e.xmotion.x_root, e.xmotion.y_root);

        if (new_target != current_target) {
          if (current_target) {
            send_msg(&ctx, current_target, ctx.atoms.Leave, ctx.src_window, 0, 0, 0, 0);
          }
          current_target = new_target;
          if (current_target) {
            send_msg(&ctx, current_target, ctx.atoms.Enter, ctx.src_window,
                      ctx.version << 24, ctx.atoms.UriList, ctx.atoms.Targets, 0);
          }
        }
        if (current_target) {
        send_msg(&ctx, current_target, ctx.atoms.Position, ctx.src_window,
                 0, (e.xmotion.x_root << 16) | (e.xmotion.y_root & 0xFFFF),
                 e.xmotion.time, ctx.atoms.ActionCopy);
        }
        break;
      }

      case Expose: {
        XDrawString(
          d, ctx.src_window, gc,
          pad, pad + font->ascent,
          display_name, strlen(display_name)
        );
        break;
      }

    case ClientMessage: {
     if (e.xclient.message_type == ctx.atoms.DndStatus) {
      LOG("Received DndStatus. Accepted: %ld\n", e.xclient.data.l[1] & 1);
     } else if (e.xclient.message_type == ctx.atoms.Finished) {
      LOG("Received Finished. Drop Successful.\n");
      dragging = 0;
     }
     break;
    }

    case ButtonRelease: {
     if (current_target) {
      LOG("Button Release. Sending Drop.\n");
      send_msg(
        &ctx, current_target, ctx.atoms.Drop, ctx.src_window,
        0, e.xbutton.time, 0, 0
      );
     } else {
      LOG("Button Release on nothing. Aborting.\n");
      dragging = 0;
     }
     XUngrabPointer(d, e.xbutton.time);
     break;
    }

    case SelectionRequest: {
     LOG("SelectionRequest for %s\n", atom_name(d, e.xselectionrequest.target));

     XSelectionEvent s = {
       .type = SelectionNotify,
       .requestor = e.xselectionrequest.requestor,
       .selection = e.xselectionrequest.selection,
       .target = e.xselectionrequest.target,
       .property = e.xselectionrequest.property,
       .time = e.xselectionrequest.time
     };

     if (e.xselectionrequest.target == ctx.atoms.Targets) {
      Atom targets[] = {ctx.atoms.Targets, ctx.atoms.UriList};
      XChangeProperty(d, s.requestor, s.property, XA_ATOM, 32,
              PropModeReplace, (unsigned char*)targets, 2);
     } else if (e.xselectionrequest.target == ctx.atoms.UriList) {
      XChangeProperty(d, s.requestor, s.property, s.target, 8,
              PropModeReplace, (unsigned char*)uri, strlen(uri));
     } else {
      s.property = None; 
     }

     XSendEvent(d, s.requestor, True, NoEventMask, (XEvent*)&s);
     XFlush(d);
     break;
    }

    default:
     LOG("Ignoring event type %d\n", e.type);
     break;

   }
  }

  return 0;
}
