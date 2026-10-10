# Canon Bullet Time

Synchronized multi-camera control for Canon cameras via EDSDK.
Designed for bullet time photography workflows.

> **It's fucking bullet time.**



## Download

[⬇️ Download for Windows](https://github.com/astronaut954/canon-bullet-time/releases/latest)



## Requirements

* Windows 10 or 11 (64-bit)
* Compatible Canon camera
* Required files in the same folder:

  * `Canon Bullet Time.exe`
  * `EDSDK.dll`
  * `EdsImage.dll`



## SDK Version

* **EDSDK:** 13.10.20 (migrating to 13.20.10)



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
2. Extract it into `third_party/edsdk/` (this folder is ignored by Git).

### Canon SDK Downloads

* **Canon Camera SDK Downloads**
  https://developercommunity.usa.canon.com/s/downloads/camera-downloads



### Developer Notes

* Current SDK: **EDSDK 13.10.20** — target: **EDSDK 13.20.10**
* Additional SDKs may be required for legacy camera support
* Refer to `CDC_EDSDK_Compat_List.pdf` for compatibility details



## License

Released under the MIT License.

> Guns. Lots of cameras.
