# Third-party notices

The window app (`Canon Bullet Time GUI`) is built with these libraries, downloaded
at build time by CMake (they are not stored in this repository):

| Library | Version | Used for | License |
| --- | --- | --- | --- |
| [webview](https://github.com/webview/webview) | 0.12.0 | Native window showing the HTML/CSS interface | MIT |
| [stb](https://github.com/nothings/stb) (`stb_image`, `stb_image_resize2`, `stb_image_write`) | commit 2c980bb | Reading, resizing and writing JPEG previews | MIT or public domain (Unlicense), at your choice |
| [LibRaw](https://www.libraw.org) | 0.21.4 | Previews of RAW photos (CR3, CR2, NEF, ARW, DNG...) | LGPL 2.1 or CDDL 1.0, at your choice — this project uses it under the **CDDL 1.0** |
| [LibRaw-cmake](https://github.com/LibRaw/LibRaw-cmake) | commit eb98e43 | Build recipe for LibRaw | LGPL 2.1 or CDDL 1.0 (same as LibRaw) |
| [Neo Terminal UI](https://github.com/Dyltom/neo-terminal-ui) | commit 0eaf72d | Design system of the window (colors, font, glow, component styles), ported to plain CSS in `assets/ui/style.css` | MIT |
| [Share Tech Mono](https://fonts.google.com/specimen/Share+Tech+Mono) | Google Fonts | Font of the window, in `assets/ui/fonts/` | SIL Open Font License 1.1 (`assets/ui/fonts/OFL.txt`) |

LibRaw is used without modifications. Its source code is available at
https://github.com/LibRaw/LibRaw (tag `0.21.4`), and its licenses are in the files
`LICENSE.CDDL` and `LICENSE.LGPL` of that repository.

The Canon EDSDK is not included in this repository; see "Canon SDK Setup" in the README.

## Neo Terminal UI — MIT License

Copyright (c) Dylan Henderson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
