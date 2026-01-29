#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD_FOLDER "build/"
#define SRC_FOLDER  "src/"
#define INCLUDE_FOLDER "include/"

#define PROG_NAME "drag"
#define PROG_VERSION "0.1.0"
#define PROG_DESC "A simple drag tool"
#define PROG_MAINTAINER "Klevis <klevisimeri11@gmail.com>"
#define PROG_LICENSE "MIT"

typedef enum {
  TARGET_X11,
  TARGET_WAYLAND
} Target_Backend;

typedef enum {
  ARCH_X86_64,
  ARCH_ARM64
} Target_Arch;

const char* get_backend_name(Target_Backend backend) {
  return (backend == TARGET_X11) ? "X11" : "Wayland";
}

const char* get_arch_suffix(Target_Arch arch) {
  return (arch == ARCH_X86_64) ? "x86_64" : "arm64";
}

const char* get_binary_name(Target_Backend backend, Target_Arch arch) {
  return nob_temp_sprintf("%s-%s-%s", PROG_NAME, get_backend_name(backend), get_arch_suffix(arch));
}

const char* get_compiler_cmd(Target_Arch arch) {
  if (arch == ARCH_ARM64) return "aarch64-linux-gnu-gcc";
  return "gcc";
}

const char* get_deb_arch(Target_Arch arch) {
  return (arch == ARCH_X86_64) ? "amd64" : "arm64";
}

const char* get_rpm_pac_arch(Target_Arch arch) {
  return (arch == ARCH_X86_64) ? "x86_64" : "aarch64";
}

bool build_program(Target_Backend backend, Target_Arch arch, bool debug) {
  Nob_Cmd cmd = {0};
  const char *output_name = get_binary_name(backend, arch);
  const char *compiler = get_compiler_cmd(arch);

  if (!nob_mkdir_if_not_exists(BUILD_FOLDER)) return false;

  nob_log(NOB_INFO, "Building %s for %s...", get_backend_name(backend), get_arch_suffix(arch));

  if (backend == TARGET_X11) {
    nob_cmd_append(
      &cmd, compiler, "-Wall", "-Wextra",
      "-o", nob_temp_sprintf("%s%s", BUILD_FOLDER, output_name),
      SRC_FOLDER"drag-X11.c",
      "-I"INCLUDE_FOLDER,
      "-lX11",
      debug ? "-DDEBUG" : "-DNODEBUG"
    );
  } else {
    nob_cmd_append(
      &cmd, compiler, "-Wall", "-Wextra",
      "-o", nob_temp_sprintf("%s%s", BUILD_FOLDER, output_name),
      SRC_FOLDER"drag-Wayland.c",
      SRC_FOLDER"xdg-shell-client-protocol.c",
      SRC_FOLDER"wlr-layer-shell-unstable-v1-protocol.c",
      SRC_FOLDER"viewporter-protocol.c",
      "-I"INCLUDE_FOLDER,
      "-lwayland-client",
      "-lwayland-cursor",
      debug ? "-DDEBUG" : "-DNODEBUG"
    );
  }

  bool success = nob_cmd_run(&cmd);
  nob_cmd_free(cmd);
  return success;
}

bool pack_apt(Target_Backend backend, Target_Arch arch) {
  const char *bin_name = get_binary_name(backend, arch);
  const char *pkg_name = "drag";
  const char *deb_arch = get_deb_arch(arch);
  const char *depends = (backend == TARGET_X11) ? "libx11-6" : "libwayland-client0, libwayland-cursor0";

  const char *dist_dir = nob_temp_sprintf("%sdeb_%s_%s", BUILD_FOLDER, get_backend_name(backend), deb_arch);
  const char *usr_bin = nob_temp_sprintf("%s/usr/bin", dist_dir);
  const char *debian_dir = nob_temp_sprintf("%s/DEBIAN", dist_dir);
  const char *control_file = nob_temp_sprintf("%s/control", debian_dir);

  nob_log(NOB_INFO, "Packaging APT: %s (%s)...", get_backend_name(backend), deb_arch);

  if (!nob_mkdir_if_not_exists(dist_dir)) return false;
  if (!nob_mkdir_if_not_exists(nob_temp_sprintf("%s/usr", dist_dir))) return false;
  if (!nob_mkdir_if_not_exists(usr_bin)) return false;
  if (!nob_mkdir_if_not_exists(debian_dir)) return false;

  if (!nob_copy_file(
    nob_temp_sprintf("%s%s", BUILD_FOLDER, bin_name),
    nob_temp_sprintf("%s/%s", usr_bin, pkg_name)
  )) return false;

  Nob_String_Builder sb = {0};
  nob_sb_appendf(&sb, "Package: %s\n", pkg_name);
  nob_sb_appendf(&sb, "Version: %s\n", PROG_VERSION);
  nob_sb_appendf(&sb, "Section: utils\n");
  nob_sb_appendf(&sb, "Priority: optional\n");
  nob_sb_appendf(&sb, "Architecture: %s\n", deb_arch);
  nob_sb_appendf(&sb, "Depends: %s\n", depends);
  nob_sb_appendf(&sb, "Maintainer: %s\n", PROG_MAINTAINER);
  nob_sb_appendf(&sb, "Description: %s (%s backend)\n", PROG_DESC, get_backend_name(backend));
  if (!nob_write_entire_file(control_file, sb.items, sb.count)) return false;
  nob_sb_free(sb);

  Nob_Cmd cmd = {0};
  nob_cmd_append(
    &cmd,
    "dpkg-deb",
    "--build",
    dist_dir,
    nob_temp_sprintf("%s%s_%s-%s_%s.deb", BUILD_FOLDER, pkg_name, PROG_VERSION, get_backend_name(backend), deb_arch)
  );
  bool ok = nob_cmd_run(&cmd);
  nob_cmd_free(cmd);
  return ok;
}

bool pack_dnf(Target_Backend backend, Target_Arch arch) {
  const char *bin_name = get_binary_name(backend, arch);
  const char *pkg_name = "drag";
  const char *rpm_arch = get_rpm_pac_arch(arch);
  const char *depends = (backend == TARGET_X11) ? "libX11" : "wayland-client, wayland-cursor";

  const char *rpm_root = nob_temp_sprintf("%srpmbuild_%s_%s", BUILD_FOLDER, get_backend_name(backend), rpm_arch);
  const char *spec_file = nob_temp_sprintf("%s/%s.spec", rpm_root, pkg_name);

  nob_log(NOB_INFO, "Packaging RPM: %s (%s)...", get_backend_name(backend), rpm_arch);

  if (!nob_mkdir_if_not_exists(rpm_root)) return false;
  if (!nob_mkdir_if_not_exists(nob_temp_sprintf("%s/SOURCES", rpm_root))) return false;
  if (!nob_mkdir_if_not_exists(nob_temp_sprintf("%s/SPECS", rpm_root))) return false;
  if (!nob_mkdir_if_not_exists(nob_temp_sprintf("%s/BUILD", rpm_root))) return false;
  if (!nob_mkdir_if_not_exists(nob_temp_sprintf("%s/RPMS", rpm_root))) return false;
  if (!nob_mkdir_if_not_exists(nob_temp_sprintf("%s/SRPMS", rpm_root))) return false;

  if (!nob_copy_file(
    nob_temp_sprintf("%s%s", BUILD_FOLDER, bin_name),
    nob_temp_sprintf("%s/SOURCES/%s", rpm_root, bin_name)
  )) return false;

  Nob_String_Builder sb = {0};
  nob_sb_appendf(&sb, "Name: %s\n", pkg_name);
  nob_sb_appendf(&sb, "Version: %s\n", PROG_VERSION);
  nob_sb_appendf(&sb, "Release: 1\n");
  nob_sb_appendf(&sb, "Summary: %s\n", PROG_DESC);
  nob_sb_appendf(&sb, "License: %s\n", PROG_LICENSE);
  nob_sb_appendf(&sb, "Requires: %s\n", depends);
  nob_sb_appendf(&sb, "BuildArch: %s\n", rpm_arch);
  nob_sb_appendf(&sb, "\n%%description\n%s (%s backend)\n", PROG_DESC, get_backend_name(backend));

  nob_sb_appendf(&sb, "\n%%install\n");
  nob_sb_appendf(&sb, "mkdir -p %%{buildroot}/usr/bin\n");
  nob_sb_appendf(&sb, "install -m 755 %%{_topdir}/SOURCES/%s %%{buildroot}/usr/bin/%s\n", bin_name, pkg_name);

  nob_sb_appendf(&sb, "\n%%files\n");
  nob_sb_appendf(&sb, "/usr/bin/%s\n", pkg_name);

  if (!nob_write_entire_file(spec_file, sb.items, sb.count)) return false;
  nob_sb_free(sb);

  const char *cwd = nob_get_current_dir_temp();
  const char *abs_rpm_root = nob_temp_sprintf("%s/%s", cwd, rpm_root);

  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, "rpmbuild", "-bb", spec_file,
      "--define", nob_temp_sprintf("_topdir %s", abs_rpm_root),
      "--target", rpm_arch
  );

  bool ok = nob_cmd_run(&cmd);
  nob_cmd_free(cmd);
  const char *old_rpm = nob_temp_sprintf("%s/RPMS/%s/%s-0.1.0-1.%s.rpm",
                                         rpm_root, rpm_arch, pkg_name, rpm_arch);
  const char *new_rpm = nob_temp_sprintf("%s%s_0.1.0-%s_%s.rpm",
                                         BUILD_FOLDER, pkg_name,
                                         get_backend_name(backend), rpm_arch);
  nob_rename(old_rpm, new_rpm);
  return ok;
}

bool pack_pacman(Target_Backend backend, Target_Arch arch) {
  const char *bin_name = get_binary_name(backend, arch);
  const char *pkg_name = "drag";
  const char *pac_arch = get_rpm_pac_arch(arch);
  const char *depends = (backend == TARGET_X11) ? "'libx11'" : "'wayland'";
  const char *arch_root = nob_temp_sprintf("%sarch_%s_%s", BUILD_FOLDER, get_backend_name(backend), pac_arch);
  const char *pkgbuild = nob_temp_sprintf("%s/PKGBUILD", arch_root);
  nob_log(NOB_INFO, "Packaging Pacman: %s (%s)...", get_backend_name(backend), pac_arch);
  if (!nob_mkdir_if_not_exists(arch_root)) return false;
  Nob_String_Builder sb = {0};
  nob_sb_appendf(&sb, "pkgname=%s\n", pkg_name);
  nob_sb_appendf(&sb, "pkgver=%s\n", PROG_VERSION);
  nob_sb_appendf(&sb, "pkgrel=1\n");
  nob_sb_appendf(&sb, "pkgdesc=\"%s (%s)\"\n", PROG_DESC, get_backend_name(backend));
  nob_sb_appendf(&sb, "arch=('%s')\n", pac_arch);
  nob_sb_appendf(&sb, "url=\"https://example.com\"\n");
  nob_sb_appendf(&sb, "license=('%s')\n", PROG_LICENSE);
  nob_sb_appendf(&sb, "depends=(%s)\n", depends);
  nob_sb_appendf(&sb, "source=()\n");
  nob_sb_appendf(&sb, "package() {\n");
  nob_sb_appendf(&sb, " mkdir -p \"$pkgdir/usr/bin\"\n");
  nob_sb_appendf(&sb, " install -m 755 \"$srcdir/../../%s\" \"$pkgdir/usr/bin/%s\"\n", bin_name, pkg_name);
  nob_sb_appendf(&sb, "}\n");
  if (!nob_write_entire_file(pkgbuild, sb.items, sb.count)) return false;
  nob_sb_free(sb);
  if (!nob_set_current_dir(arch_root)) return false;
  Nob_Cmd cmd = {0};
  // -f: force overwrite
  // -A: ignore architecture (we are cross-packaging)
  // -d: no dependencies (skip pacman db check on Fedora)
  nob_cmd_append(&cmd, "makepkg", "-f", "-A", "-d");
  bool ok = nob_cmd_run(&cmd);
  nob_cmd_free(cmd);
  nob_set_current_dir("../../");
  
  const char *old_pkg = nob_temp_sprintf("%s/%s-%s-1-%s.pkg.tar.gz",
                                         arch_root, pkg_name, PROG_VERSION, pac_arch);
  const char *new_pkg = nob_temp_sprintf("%s%s_%s-%s_%s.pkg.tar.gz",
                                         BUILD_FOLDER, pkg_name, PROG_VERSION,
                                         get_backend_name(backend), pac_arch);
  if (ok) {
    if (!nob_rename(old_pkg, new_pkg)) {
      nob_log(NOB_WARNING, "Failed to rename package from %s to %s", old_pkg, new_pkg);
      return false;
    }
  }

  return ok;
}

void print_usage(const char *program) {
  nob_log(NOB_INFO, "Usage:");
  nob_log(NOB_INFO, "  %s all", program);
  nob_log(NOB_INFO, "  %s <backend> [arch] [DEBUG]", program);
  nob_log(NOB_INFO, "  %s dist <manager> <backend> [arch]", program);
}

Target_Arch parse_arch(const char *str) {
  if (str && strcmp(str, "arm64") == 0) return ARCH_ARM64;
  return ARCH_X86_64;
}

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);
  const char *program = nob_shift(argv, argc);

  if (argc <= 0) {
    print_usage(program);
    return 1;
  }

  const char *arg1 = nob_shift(argv, argc);

  if (strcmp(arg1, "all") == 0) {
    Target_Backend backends[] = {TARGET_X11, TARGET_WAYLAND};
    Target_Arch archs[] = {ARCH_X86_64 /*, ARCH_ARM64*/};

    for (int b = 0; b < 2; ++b) {
      for (int a = 0; a < 2; ++a) {
        Target_Backend bk = backends[b];
        Target_Arch ar = archs[a];

        if (!build_program(bk, ar, false)) {
          return 1;
        }
        if (!pack_apt(bk, ar)) return 1;
        if (!pack_dnf(bk, ar)) return 1;
        if (!pack_pacman(bk, ar)) return 1;
      }
    }
    return 0;
  }

  if (strcmp(arg1, "dist") == 0) {
    if (argc < 2) {
      print_usage(program);
      return 1;
    }
    const char *manager = nob_shift(argv, argc);
    const char *backend_str = nob_shift(argv, argc);
    const char *arch_str = (argc > 0) ? nob_shift(argv, argc) : "x86_64";

    Target_Backend backend;
    if (strcmp(backend_str, "X11") == 0) backend = TARGET_X11;
    else if (strcmp(backend_str, "Wayland") == 0) backend = TARGET_WAYLAND;
    else return 1;

    Target_Arch arch = parse_arch(arch_str);

    if (!build_program(backend, arch, false)) return 1;

    if (strcmp(manager, "apt") == 0) return !pack_apt(backend, arch);
    if (strcmp(manager, "dnf") == 0) return !pack_dnf(backend, arch);
    if (strcmp(manager, "pacman") == 0) return !pack_pacman(backend, arch);

    return 1;
  }

  Target_Backend backend;
  if (strcmp(arg1, "X11") == 0) backend = TARGET_X11;
  else if (strcmp(arg1, "Wayland") == 0) backend = TARGET_WAYLAND;
  else {
    print_usage(program);
    return 1;
  }

  Target_Arch arch = ARCH_X86_64;
  bool debug = false;

  while (argc > 0) {
    const char *arg = nob_shift(argv, argc);
    if (strcmp(arg, "arm64") == 0) arch = ARCH_ARM64;
    else if (strcmp(arg, "x86_64") == 0) arch = ARCH_X86_64;
    else if (strcmp(arg, "DEBUG") == 0) debug = true;
  }

  if (!build_program(backend, arch, debug)) return 1;

  return 0;
}
