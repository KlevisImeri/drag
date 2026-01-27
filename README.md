# filedrag

A lightweight C utility that allows you to initiate a drag-and-drop event from
the command line on Linux (X11).

## Usage
Once installed, you can run the program from anywhere:

```bash
filedrag /path/to/your/file.png
```

1.  A window will appear under your cursor displaying the file name.
2.  Drag and drop it into another application (Browser, Discord, File Manager, etc.).

## Dependencies
You need the X11 development library to compile this project.

*   **Debian/Ubuntu/Kali:** `sudo apt install libx11-dev`
*   **Arch Linux:** `sudo pacman -S libx11`
*   **Fedora:** `sudo dnf install libX11-devel`

## Build & Install
This project uses a Makefile for easy compilation and installation.

### Compile
To compile the binary locally:
```bash
make
```

### Install
To compile and install the binary to `/usr/local/bin` (requires sudo password):
```bash
make install
```
*Note: The Makefile handles the `sudo` command internally.*

### Clean
To remove the compiled binary from the current directory:
```bash
make clean
```

## Development & Debugging

### Test
To run the program using its own source code as the drag target:
```bash
make test
```

### Debug Mode
To compile with verbose logging enabled and run the test immediately:
```bash
make debug
```
*This enables the internal `LOG` macro to print event details to the console.*
