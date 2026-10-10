# Third-party notices

The window app (`Canon Bullet Time GUI`) is built with these libraries, downloaded
at build time by CMake (they are not stored in this repository):

| Library | Version | Used for | License |
| --- | --- | --- | --- |
| [webview](https://github.com/webview/webview) | 0.12.0 | Native window showing the HTML/CSS interface | MIT |
| [stb](https://github.com/nothings/stb) (`stb_image`, `stb_image_resize2`, `stb_image_write`) | commit 2c980bb | Reading, resizing and writing JPEG previews | MIT or public domain (Unlicense), at your choice |
| [LibRaw](https://www.libraw.org) | 0.21.4 | Previews of RAW photos (CR3, CR2, NEF, ARW, DNG...) | LGPL 2.1 or CDDL 1.0, at your choice — this project uses it under the **CDDL 1.0** |
| [LibRaw-cmake](https://github.com/LibRaw/LibRaw-cmake) | commit eb98e43 | Build recipe for LibRaw | LGPL 2.1 or CDDL 1.0 (same as LibRaw) |

LibRaw is used without modifications. Its source code is available at
https://github.com/LibRaw/LibRaw (tag `0.21.4`), and its licenses are in the files
`LICENSE.CDDL` and `LICENSE.LGPL` of that repository.

The Canon EDSDK is not included in this repository; see "Canon SDK Setup" in the README.
