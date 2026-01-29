# drag

A super lightweight C utility that allows you to initiate a drag-and-drop event from
the command line on Linux. It supports both X11 and Wayland natively, with no GTK 
or Qt dependencies—just pure X11 and Wayland libraries.

## Installation

You can install prebuilt binaries or packages from the [releases
page](https://github.com/KlevisImeri/drag/releases/), or you can build it
yourself using the [nob.h](https://github.com/tsoding/nob.h) library.
Run `./nob` to see usage instructions for building different binaries and
packages.

**Note:** ARM builds are not supported yet.

## Usage

```bash
drag /path/to/your/file.png
```

1. Move your mouse slightly. A window will appear under your cursor displaying the file name.
2. Drop the file:
   * **X11:** Drop it wherever you want (just move the mouse).
   * **Wayland:** Drag (you have to keep your mouse clicked) and drop it into another application (browser, Discord, file manager, etc.).


