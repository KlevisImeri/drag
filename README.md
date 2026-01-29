# drag

A lightweight C utility that allows you to initiate a drag-and-drop event from
the command line on Linux (X11).

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
   * **X11:** Drop it wherever you want.
   * **Wayland:** Drag and drop it into another application (browser, Discord, file manager, etc.).


