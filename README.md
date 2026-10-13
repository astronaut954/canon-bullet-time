# Canon Bullet Time

Synchronized multi-camera control for Canon cameras via EDSDK.
Designed for bullet time photography workflows.

> **It's fucking bullet time.**



## Download

[⬇️ Download for Windows and macOS](https://github.com/astronaut954/canon-bullet-time/releases/latest)



## Requirements

**Windows**

* Windows 10 or 11 (64-bit)
* Compatible Canon camera
* Keep these files in the same folder: `Canon Bullet Time.exe`, `EDSDK.dll`, `EdsImage.dll`, `assets/`

**macOS**

* Mac with Apple Silicon (M1 or newer), macOS 13.3 or later
* Compatible Canon camera
* Keep these files in the same folder: `Canon Bullet Time`, `Frameworks/`, `assets/`
* The app is not signed by Apple yet. After extracting the zip, run once in Terminal:

  ```
  xattr -dr com.apple.quarantine path/to/canon-bullet-time-vX.Y.Z
  ```

  Then start it from Terminal: `./"Canon Bullet Time"`



## SDK Version

* **EDSDK:** 13.20.10



## How to Use

1. Extract all release files into the same folder
2. Connect your Canon cameras via USB
3. Run `Canon Bullet Time.exe`
4. Follow the terminal instructions
5. Enter the Matrix



## Troubleshooting

If no camera is found, check:

* Turn the camera off and on again
* Camera is on and not in movie mode
* USB cable supports data (not charge-only)
* No other app is using the camera (EOS Utility, Photos, Image Capture, Google Drive)
* Your model is supported (see below)

The app also warns when a connected model is not on Canon's compatibility list,
and explains the most common connection and shooting errors.



## Compatibility

Check camera compatibility in:

`CDC_EDSDK_Compat_List.pdf`

Support depends on the EDSDK version used.



## Notes

* Compiled for **x64 architecture**
* Do **not** move the DLLs elsewhere
* If the DLLs are missing, there is no bullets



## For Developers

Want to build, modify, or extend the project?

You may need to obtain the appropriate Canon SDK depending on the cameras you want to support.

### Development Roadmap

This project is being organized for Windows (x64) and macOS (Apple Silicon) support.

* [Roadmap](ROADMAP.md) — stages, decisions and platform status
* [Agent rules](AGENTS.md) — working rules for AI coding agents
* [Manual test checklist](docs/teste-manual.md)

### Canon SDK Setup

The Canon EDSDK is **not included** in this repository (Canon's license only allows
distributing it inside the compiled app). To build:

1. Download **EDSDK 13.20.10** for your platform from the link below.
2. Install it into `third_party/edsdk/` (this folder is ignored by Git):

   ```
   python scripts/setup_edsdk.py path/to/EDSDK_v13.20.10_Windows.zip      # Windows
   python3 scripts/setup_edsdk.py path/to/EDSDK_v13.20.10_Macintosh.zip   # macOS
   ```

### Build (Windows x64)

Requires Visual Studio with the "Desktop development with C++" workload (includes CMake).
From a *Developer Command Prompt*:

```
cmake --preset windows-x64
cmake --build --preset windows-x64-release
```

Two apps are generated in `build/windows-x64/Release/`: `Canon Bullet Time.exe` (terminal) and `Canon Bullet Time GUI.exe` (window), with the SDK DLLs and `assets/` next to them. The first configure downloads the webview, stb and LibRaw libraries and Microsoft WebView2 headers (internet required).

### Build (macOS, Apple Silicon)

Requires Xcode Command Line Tools (`xcode-select --install`) and CMake (`brew install cmake`).

```
cmake --preset macos-arm64
cmake --build --preset macos-arm64-release
```

Two apps are generated in `build/macos-arm64/`: `Canon Bullet Time` (terminal) and `Canon Bullet Time GUI` (window), with `Frameworks/EDSDK.framework` and `assets/` next to them. The first configure downloads the webview, stb and LibRaw libraries (internet required).

The window has two looks to compare, both with the structure of [Nocturne](https://github.com/starside-io/nocturne)
and the colors of [Neo Terminal UI](https://github.com/Dyltom/neo-terminal-ui); switch them at the top
of the window (the choice is remembered):

* **Mix 1** — Nocturne's fonts (Rajdhani and JetBrains Mono), glow only on highlights, in `assets/ui/themes/nocturne-neo-1.css`
* **Mix 2** — Neo Terminal's font (Share Tech Mono), text glow, scanlines and blinking cursor, in `assets/ui/themes/nocturne-neo-2.css`

Colors, fonts and effects are variables at the top of each file. The fonts are in
`assets/ui/fonts/` and are embedded in the page when the window opens (works offline).

### Window app: where photos go

The window app keeps every session in its own folder until you choose where to save:

* Windows: `%LOCALAPPDATA%\Canon Bullet Time\Photos`
* macOS: `~/Library/Application Support/Canon Bullet Time/Photos`

**Save sessions** moves them to the folder you choose. Closing the window with unsaved
sessions asks whether to save or delete them; if the app closes unexpectedly, the photos
stay in that folder and the app offers to save them the next time it opens.

Cameras connect by themselves, one at a time (the Canon SDK accepts no other command while
a camera is connecting, so shooting waits until it finishes). A camera that is turned off
and on again returns to its place in the order. If a camera
is on and plugged in but does not connect, use **Restart camera connections** in the Activity panel.

Sessions are shown on the main screen: folders on the left, the photos of the selected
session in a strip on top and the selected photo large below. **Reorder by photo** (in
the Cameras panel) takes a test photo with each camera, without sound and without
creating a session; drag the photos into the order the cameras are set up and save.

The session viewer shows JPEG and RAW photos (CR3, CR2, NEF, ARW, DNG and other formats
read by LibRaw), using the preview image the camera stores inside each RAW file.

The terminal app keeps working as before (photos go straight to `Downloads/bullet-time`).

### Updating the camera compatibility list

The model list used by the app is generated from `docs/CDC_EDSDK_Compat_List.pdf`:

```
pip install pdfplumber
python3 scripts/update_compat_list.py v13.20.0
```

Regional model names (e.g. Rebel T7 = EOS 2000D = Kiss X90) are mapped in
`src/core/sdk_diagnostics.cpp`.

### Publishing a release

Each platform packages its own zip. Run on each machine, after building:

```
python3 scripts/publish_release.py vX.Y.Z   # macOS — creates the tag and the GitHub release
python scripts\publish_release.py vX.Y.Z    # Windows — attaches the Windows zip to the same release
```

On Windows, run it from a terminal opened on the PC itself: over SSH, Git and `gh` cannot read the Windows credential store.

### Canon SDK Downloads

* **Canon Camera SDK Downloads**
  https://developercommunity.usa.canon.com/s/downloads/camera-downloads



### Developer Notes

* Current SDK: **EDSDK 13.20.10**
* Additional SDKs may be required for legacy camera support
* Refer to `CDC_EDSDK_Compat_List.pdf` for compatibility details



## License

Released under the MIT License. Third-party libraries used by the window app are listed
in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

> Guns. Lots of cameras.
