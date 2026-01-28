#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD_FOLDER "build/"
#define SRC_FOLDER   "src/"
#define INCLUDE_FOLDER "include/"

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);
  if (!nob_mkdir_if_not_exists(BUILD_FOLDER)) return 1;

  Nob_Cmd cmd = {0};
  
  if(argc == 1) {
    nob_log(NOB_INFO, "nob (X11 | Wayland)");
    return 1;
  }
  nob_shift(argv, argc);
  const char* display_server = nob_shift(argv, argc);

  if(strcmp(display_server,"X11")==0) {
    nob_cmd_append(
      &cmd,
      "gcc",
      "-Wall",
      "-Wextra",
      "-o",
      BUILD_FOLDER"drag-X11",
      SRC_FOLDER"drag-X11.c",
      "-lX11"
    );
  } else if (strcmp(display_server,"Wayland")==0) {
    nob_cmd_append(
      &cmd,
      "gcc",
      "-Wall",
      "-Wextra",
      "-o",
      BUILD_FOLDER"drag-Wayland",
      SRC_FOLDER"drag-Wayland.c",
      SRC_FOLDER"xdg-shell-client-protocol.c", 
      SRC_FOLDER"wlr-layer-shell-unstable-v1-protocol.c",
      "-I"INCLUDE_FOLDER,
      "-lwayland-client",
      "-lwayland-cursor"
    );
  } else {
    nob_log(NOB_WARNING, "The avaliable display servers are X11 or Wayland.");
    nob_log(NOB_INFO, "nob (X11 | Wayland)");
    return 1;
  }
  if (!nob_cmd_run(&cmd)) return 1;
  return 0;
}
