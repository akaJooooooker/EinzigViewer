# EinzigViewer v1.0.0

[繁體中文](README.md) | **English**

A native Windows image and EPUB image viewer with single-page and continuous reading, local AI upscaling, and NVIDIA RTX VSR/HDR.

## Downloads and installation

Choose an edition from the [v1.0.0 release page](https://github.com/akaJooooooker/EinzigViewer/releases/tag/v1.0.0):

- **Setup.exe installer**: installs for the current user, with Start menu shortcuts and an uninstaller. Administrator rights are not required.
- **Portable ZIP**: extract everything into a writable, permanent folder, then run EinzigViewer.exe. Keep all DLLs, the ai directory and portable.ini; settings are stored in the adjacent settings.ini.

Supports Windows 10/11 x64. Ordinary viewing does not require NVIDIA hardware. AI requires a working Vulkan GPU and driver; RTX VSR/HDR requires compatible NVIDIA RTX hardware. HDR also requires a suitable display and Windows HDR enabled. AI models and the required Visual C++ Runtime files are bundled; models are not downloaded on startup.

## AI is still being tested and improved

AI starts off. Press **A** to enable it manually. Originals appear first, then completed results replace them; press A again to restore originals immediately. Choose **2×/4×** and hold **C** to compare. Preprocessing covers the current folder or EPUB within resource budgets. Single-page and continuous reading reuse valid completed results.

Speed and quality remain under improvement. **First-time processing can take a noticeable amount of time, especially with large images and 4×; real-time processing is not guaranteed.** Time depends on image dimensions, scale, hardware and other workloads. Fast cached switching does not mean new images process instantly. AI may alter tiny text, screentones and edges, and cannot recover true details absent from the original; you can return to the original at any time.

AI uses local Real-CUGAN models-se without uploading images. Keep the complete ai/realcugan directory; missing files, insufficient resources or processing failures retain the original and show a reason. AI uses SDR and is mutually exclusive with RTX VSR/HDR.

AI → Resource settings provides automatic or manual RAM, disk-cache, child CPU, batch and free-VRAM controls. Work still preserves current resource reserves and pauses when resources are insufficient. The VRAM reserve is a monitoring threshold; raising resource ceilings does not guarantee proportionally faster processing. Limits persist, while AI still starts off on each launch.

## Controls

| Key | Action |
| --- | --- |
| O / drag-drop | Open images or EPUB; automatically index images in the same folder |
| V | Single-page / continuous reading |
| 1 / 2 / 3 | Fit window / fit width / actual size |
| Ctrl+wheel, + / - | Zoom |
| Arrows, Page Up/Down, Space | Navigate or scroll |
| [ / ] | Rotate the current page left/right by 90° without rewriting its file |
| Ctrl+C | Copy the rotated SDR original |
| Ctrl+Shift+C | Drag to copy a displayed region, including adjacent continuous pages; Esc cancels |
| Ctrl+S / Ctrl+Shift+S | Export original PNG / downscale and export |
| A / C | Toggle AI / hold to compare with the original |
| R / H | Single-page RTX enhancement / HDR |
| M / F11 | Maximize / fullscreen |

Supports WIC image formats, EXIF orientation, natural sorting, background preloading, Chinese/English UI and EPUB spine image order. EPUB displays embedded raster images without text/CSS layout or DRM decryption. Continuous reading uses SDR.

## Set as the Windows default image viewer

- **Installed**: Help → Set as default image viewer opens Windows Settings. Select EinzigViewer and confirm the formats it should open. Installing does not change existing defaults.
- **Portable**: extract to a fixed location, then right-click an image → Open with → Choose another app. Select EinzigViewer.exe and choose always use. Confirm each extension separately; moving the folder requires selecting its new path.

## Updates and settings

Both editions notify about newer GitHub Releases. Use Help to check manually or disable automatic checks. Background checks run at most once per 24 hours; choose Download, Later or Skip when a new release appears. Downloads open a browser and do not replace files automatically; close the viewer before upgrading. Preserve portable settings.ini when updating. Version 0.7.0 has no update notifications and needs one manual upgrade.

Installed settings use %LOCALAPPDATA%/EinzigViewer/settings.ini; portable settings remain next to the executable. Third-party terms accompany LICENSES and ai/realcugan/licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).

## Building

Use Visual Studio 2022 C++, Windows SDK and CMake 3.24+. Run cmake -S . -B build, then cmake --build build --config Release. EINZIGVIEWER_RTX_SDK_ROOT selects the optional NVIDIA SDK; the ordinary viewer can also be built without it. scripts/Fetch-AiModels.ps1 prepares pinned AI assets, and scripts/Package-Release.ps1 creates Setup/ZIP packages.
