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

The app is generated in `build/windows-x64/Release/`, with the SDK DLLs and audio assets next to it.

### Build (macOS, Apple Silicon)

Requires Xcode Command Line Tools (`xcode-select --install`) and CMake (`brew install cmake`).

```
cmake --preset macos-arm64
cmake --build --preset macos-arm64-release
```

The app is generated in `build/macos-arm64/`, with `Frameworks/EDSDK.framework` and audio assets next to it.

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

Released under the MIT License.

> Guns. Lots of cameras.
