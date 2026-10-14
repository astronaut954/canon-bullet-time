# Stillpoint — Bullet Time Photography

Stillpoint fires several cameras at the same instant to create bullet time photos
(the frozen-moment effect from *The Matrix*). Synchronized multi-camera control for
Canon cameras via EDSDK; other brands are planned.

Formerly **Canon Bullet Time**.

> **It's fucking bullet time.**



## Download

[⬇️ Download for Windows and macOS](https://github.com/astronaut954/stillpoint-bullet-time-photography/releases/latest)

The apps are on the **Releases** page (link above), under *Assets* — not in the source code files.

| System | File | How to install |
| --- | --- | --- |
| macOS (Apple Silicon) | `stillpoint-vX.Y.Z-macos-arm64.dmg` | Open it and drag Stillpoint to Applications |
| Windows 10/11 (64-bit) | `stillpoint-vX.Y.Z-windows-x64-setup.exe` | Run it and follow the steps |

The `.zip` files have the same apps without an installer (on macOS, the zip also has the terminal app).

### First launch on macOS

Stillpoint is not registered with Apple yet, so macOS blocks it the first time. Allow it once:

1. Double-click Stillpoint in Applications. macOS says it could not verify the app: click **Done** (or **OK**).
2. Open **System Settings → Privacy & Security**.
3. Scroll down to **Security** and click **Open Anyway** next to the message about Stillpoint.
4. Confirm with your password or Touch ID, then click **Open Anyway** again.

The same steps are in *Read Me*, inside the `.dmg`. After installing a new version you may need to repeat them.

### First launch on Windows

Windows may show **"Windows protected your PC"** when you run the installer, because it is not
signed with a paid certificate. Click **More info**, then **Run anyway**.

The installer can also add the terminal app (optional, unchecked by default). Uninstall from
**Settings → Apps**; your photos in `%LOCALAPPDATA%\Stillpoint` are kept.



## Requirements

**Windows**

* Windows 10 or 11 (64-bit)
* Compatible Canon camera
* Microsoft Edge WebView2 (already included in up-to-date Windows 10 and 11)

**macOS**

* Mac with Apple Silicon (M1 or newer), macOS 13.3 or later
* Compatible Canon camera

The Canon SDK files come inside the app; you do not need to install anything from Canon.

### Zip version (no installer)

* **Windows:** keep `Stillpoint.exe`, `EDSDK.dll`, `EdsImage.dll` and `assets/` in the same folder.
  The terminal app is in `Terminal App/`, with its own copies of those files.
* **macOS:** `Stillpoint.app` has everything inside it. The terminal app is in `Terminal App/`:
  keep `Stillpoint Terminal App`, `Frameworks/` and `assets/` together and run
  `./"Stillpoint Terminal App"` there in Terminal. If macOS blocks it, use **Open Anyway**
  as above.

Alternative for the macOS zip (Terminal), to allow everything in the folder at once:

```
xattr -dr com.apple.quarantine path/to/stillpoint-vX.Y.Z
```



## SDK Version

* **EDSDK:** 13.20.10



## How to Use

1. Install Stillpoint (see Download above)
2. Connect your Canon cameras via USB
3. Open `Stillpoint` (the terminal app is optional: `Terminal App` folder)
4. In the window app, the cameras connect by themselves; press "It's bullet time".
   In the terminal app, follow the instructions on screen
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

Two apps are generated in `build/windows-x64/Release/`: `Stillpoint.exe` (window) and `Stillpoint Terminal App.exe` (terminal; in the release package it goes to `Terminal App/`), with the SDK DLLs and `assets/` next to them. The first configure downloads the webview, stb and LibRaw libraries and Microsoft WebView2 headers (internet required).

### Build (macOS, Apple Silicon)

Requires Xcode Command Line Tools (`xcode-select --install`) and CMake (`brew install cmake`).

```
cmake --preset macos-arm64
cmake --build --preset macos-arm64-release
```

Two apps are generated in `build/macos-arm64/`: `Stillpoint` (window; in the release package it goes inside `Stillpoint.app`) and `Stillpoint Terminal App` (terminal; in the release package it goes to `Terminal App/`), with `Frameworks/EDSDK.framework` and `assets/` next to them. The first configure downloads the webview, stb and LibRaw libraries (internet required).

The window's look combines the structure of [Nocturne](https://github.com/starside-io/nocturne)
(layout, panel rails, fonts Rajdhani and JetBrains Mono) with the colors of
[Neo Terminal UI](https://github.com/Dyltom/neo-terminal-ui), with square corners. It is in
`assets/ui/themes/nocturne-neo.css`.

Colors, fonts and effects are variables at the top of that file. The fonts are in
`assets/ui/fonts/` and are embedded in the page when the window opens (works offline).

### Window app: where photos go

The window app keeps every session in its own folder until you choose where to save:

* Windows: `%LOCALAPPDATA%\Stillpoint\Photos`
* macOS: `~/Library/Application Support/Stillpoint/Photos`

**Save sessions** moves them to the folder you choose. Closing the window with unsaved
sessions asks whether to save or delete them; if the app closes unexpectedly, the photos
stay in that folder and the app offers to save them the next time it opens.
Sessions kept by the old version (folder `Canon Bullet Time`) move to the new folder the
first time Stillpoint opens.

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

### App icon

The icon files are in `resources/icon/` (generated, kept in Git). To change the drawing,
edit `scripts/make_icon.py` and run:

```
pip install cairosvg pillow
python3 scripts/make_icon.py
```

Windows embeds `app-icon.ico` in both `.exe` files at build time; the macOS release
script copies `app-icon.icns` into the `.app`.

### Publishing a release

Each platform packages its own zip and installer (`.dmg` on macOS, `-setup.exe` on Windows).
Run on each machine, after building:

```
python3 scripts/publish_release.py vX.Y.Z   # macOS — the first one creates the tag and the GitHub release
python scripts\publish_release.py vX.Y.Z    # Windows — the second one attaches its files to the same release
```

Tools needed once:

* macOS: nothing to install by hand. The first run creates `build/release-tools` and installs
  [dmgbuild](https://github.com/dmgbuild/dmgbuild) there (internet required).
* Windows: [Inno Setup](https://jrsoftware.org/isinfo.php) 6.5.2 or newer
  (`winget install JRSoftware.InnoSetup`).

The installer images and the macOS *Read Me* are in `resources/installer/`, generated by
`scripts/make_installer_art.py` (see the top of that script). The Windows installer script is
`resources/installer/stillpoint.iss`.

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
