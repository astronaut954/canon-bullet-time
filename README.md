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

* **EDSDK:** 13.20.20



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
* If the DLLs are missing, there is no spoon



## For Developers

Want to build, modify, or extend the project?

You may need to obtain the appropriate Canon SDK depending on the cameras you want to support.

### Canon SDK Downloads

* **Canon Camera SDK Downloads**
  https://developercommunity.usa.canon.com/s/downloads/camera-downloads



### Developer Notes

* Current target SDK: **EDSDK 13.20.20**
* Additional SDKs may be required for legacy camera support
* Refer to `CDC_EDSDK_Compat_List.pdf` for compatibility details



## License

Released under the MIT License.

> Guns. Lots of cameras.