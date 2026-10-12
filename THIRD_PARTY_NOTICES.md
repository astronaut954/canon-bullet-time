# Third-party notices

The window app (`Canon Bullet Time GUI`) is built with these libraries, downloaded
at build time by CMake (they are not stored in this repository):

| Library | Version | Used for | License |
| --- | --- | --- | --- |
| [webview](https://github.com/webview/webview) | 0.12.0 | Native window showing the HTML/CSS interface | MIT |
| [stb](https://github.com/nothings/stb) (`stb_image`, `stb_image_resize2`, `stb_image_write`) | commit 2c980bb | Reading, resizing and writing JPEG previews | MIT or public domain (Unlicense), at your choice |
| [LibRaw](https://www.libraw.org) | 0.21.4 | Previews of RAW photos (CR3, CR2, NEF, ARW, DNG...) | LGPL 2.1 or CDDL 1.0, at your choice — this project uses it under the **CDDL 1.0** |
| [LibRaw-cmake](https://github.com/LibRaw/LibRaw-cmake) | commit eb98e43 | Build recipe for LibRaw | LGPL 2.1 or CDDL 1.0 (same as LibRaw) |
| [Neo Terminal UI](https://github.com/Dyltom/neo-terminal-ui) | commit 0eaf72d | "Neo Terminal" theme of the window (colors, font, glow, component styles), ported to plain CSS in `assets/ui/themes/neo-terminal.css` | MIT |
| [Nocturne](https://github.com/starside-io/nocturne) | commit 9693bd5 | "Nocturne" theme of the window: tokens from nocturne-tokens and component styles from nocturne-css, adapted to the app's classes in `assets/ui/themes/nocturne.css` | Apache 2.0 (`assets/ui/themes/LICENSE-nocturne.txt`) |
| [CYBERCORE CSS](https://github.com/sebyx07/cybercore-css) | commit 5b90ffb | "Cybercore" theme of the window: tokens from `core/_variables.scss` and component styles (button, card, badge, input, modal, tabs, alert, scanlines, noise), adapted to the app's classes in `assets/ui/themes/cybercore.css` | MIT |
| [Share Tech Mono](https://fonts.google.com/specimen/Share+Tech+Mono) | Google Fonts | Font of the Neo Terminal theme, in `assets/ui/fonts/` | SIL Open Font License 1.1 (`assets/ui/fonts/OFL-ShareTechMono.txt`) |
| [Rajdhani](https://fonts.google.com/specimen/Rajdhani) | Google Fonts | Font of the Nocturne and Cybercore themes (Regular, SemiBold, Bold), reduced to Latin characters | SIL Open Font License 1.1 (`assets/ui/fonts/OFL-Rajdhani.txt`) |
| [JetBrains Mono](https://www.jetbrains.com/lp/mono/) | Google Fonts | Mono font of the Nocturne and Cybercore themes, reduced to Latin characters | SIL Open Font License 1.1 (`assets/ui/fonts/OFL-JetBrainsMono.txt`) |
| [Exo 2](https://fonts.google.com/specimen/Exo+2) | Google Fonts | Text font of the Cybercore theme, reduced to Latin characters | SIL Open Font License 1.1 (`assets/ui/fonts/OFL-Exo2.txt`) |
| [Orbitron](https://fonts.google.com/specimen/Orbitron) | Google Fonts | Logo and countdown font of the Cybercore theme, unmodified (Reserved Font Name "Orbitron") | SIL Open Font License 1.1 (`assets/ui/fonts/OFL-Orbitron.txt`) |

LibRaw is used without modifications. Its source code is available at
https://github.com/LibRaw/LibRaw (tag `0.21.4`), and its licenses are in the files
`LICENSE.CDDL` and `LICENSE.LGPL` of that repository.

The Canon EDSDK is not included in this repository; see "Canon SDK Setup" in the README.

## Nocturne — NOTICE (Apache License 2.0)

```
Nocturne Design System
Copyright 2026 Mederic Burlet

This product includes software developed as part of NocturneOS
(https://github.com/starside-io/nocturne-os).
```

Changes made here: the tokens and component styles were rewritten to style the
elements of this app's page (`assets/ui/themes/nocturne.css`); no Nocturne file
is shipped unchanged. Full license text: `assets/ui/themes/LICENSE-nocturne.txt`.

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

## CYBERCORE CSS — MIT License

Copyright (c) 2026 sebyx07

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
