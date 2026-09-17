# EinzigViewer

[繁體中文](README.md) | **English**

EinzigViewer is a native Windows image viewer designed for enlarging comic text, linework, and general images, with integrated NVIDIA RTX Video VSR and HDR. VSR is super-resolution enhancement, not content restoration: it cannot reconstruct details that do not exist in the source, but it is often especially helpful for small text, screentones, and edges in low-resolution comics. After opening an image, EinzigViewer automatically indexes the same folder, applies natural filename sorting, and pre-decodes the eight neighboring images in each direction.

Current version: **0.7.0: gap-free continuous vertical reading, EPUB image reading, tiled tall-image rendering, and cache reliability improvements**. HDR JPEG XR to SDR PNG, downscaling, and single-page RTX VSR/HDR remain available.

## 0.7.0: Continuous reading and EPUB

Press **V**, or choose View / right-click → Continuous vertical reading (no gaps). Images fit the reading width and join edge to edge. Wheel and precision touchpad input scroll continuously; drag to pan and Ctrl+wheel to zoom. Up/Down move a little; Page Up/Page Down/Space scroll about one screen; Shift+Space scrolls upward. Left/Right jump between images, and Home/End reach the beginning/bottom. Press 1/2/3 to return to single-page display. The reading mode is remembered.

Open or drop an `.epub` to load it in the background and automatically enter continuous mode. Images follow the EPUB spine and their order within each chapter, including XHTML `img` and raster images in SVG wrappers. Repeated page appearances are preserved; filenames do not reorder the book. Esc cancels import, and opening another file supersedes it. Failed imports preserve the current content. No external resources are fetched and no scripts execute.

The information bar identifies the image at the top of the viewport. Ctrl+S / Ctrl+Shift+S export or downscale that page as PNG. Extracted images live in a unique temporary directory per book, removed after switching books once in-flight decoders release it, or on normal exit.

Continuous reading currently uses **SDR** and needs no NVIDIA GPU. R/H return to single-page RTX VSR/HDR; V returns from RTX to continuous SDR with a notice. Multi-page HDR composition is not implemented yet.

The decoded cache has a hard 768 MiB budget and prioritizes visible pages. An additional 96 MiB Direct2D tile cache uploads only visible sections of tall images. These budgets exclude active decoding buffers and the current displayed image. Failed images are not repeatedly retried while scrolling; reopening retries them. Individual decode buffers are limited to 512 MiB to reject excessive dimensions.

EPUB support is for images, without text/CSS layout, vector-only SVG, or DRM decryption. Font obfuscation does not block image reading. ZIP limits: 50,000 entries, 256 MiB unpacked per entry, 4 GiB total. Unsupported books produce an error. Reading order follows [W3C EPUB 3.3](https://www.w3.org/TR/epub-33/).

> **Project rename:** Starting with 0.6.0, the project is renamed from Eidolyx to **EinzigViewer**; version 0.4.4 and earlier used MiraView. *Einzig* means unique in German, and Viewer describes the application's purpose. When no new settings file exists, the first launch imports settings from Eidolyx and then MiraView; settings are subsequently saved to `%LOCALAPPDATA%\EinzigViewer\settings.ini`.

## Implemented Features

- Native C++20/Win32 application with no .NET Runtime requirement.
- WIC decoding for JPG, PNG, BMP, the first frame of GIF, TIFF, ICO, JPEG XR, and formats such as WebP, HEIC, and AVIF when a corresponding system WIC codec is installed.
- Fixed-point or floating-point extended-RGB JXR/WDP/HDP is decoded through 128bpp float scRGB before luminance-preserving HDR-to-SDR tone mapping and sRGB encoding, instead of being reduced to 8-bit at the start of decoding.
- File → Export PNG saves the fully decoded current image—including the SDR result of an HDR JXR—at its original size with an sRGB PNG marker. It never saves the RTX presentation enhancement by mistake.
- File → Downscale and Export PNG accepts a target long edge, preserves the aspect ratio, and uses high-quality WIC Fant downscaling. It cannot accidentally enlarge the source.
- EXIF Orientation support for automatic JPEG rotation.
- Automatic same-folder indexing after opening one image.
- Windows logical natural sorting: `1, 2, 3, 10` instead of `1, 10, 2, 3`.
- Four background image-decoding workers.
- Direction-aware prefetching of eight images before and after the current image, prioritizing the next image.
- A hard 768 MiB LRU decoded-image cache, prioritizing the current pages while still reclaiming entries over budget.
- Obsolete queued work is discarded during rapid navigation, keeping the current image at the highest priority.
- Direct2D rendering, fit-to-window, fit-to-width, 100% view, mouse panning, and cursor-centered zooming.
- Native Windows menu bar (File, Navigate, View, RTX, and Help), context menu, and drag-and-drop opening.
- The main window initially opens centered in the current monitor's work area at 960×640, then remembers user-adjusted position and size.
- Dedicated EinzigViewer application icon embedded in the executable and title bar.
- Fullscreen mode and an image index/dimensions/zoom information bar; the menu bar is hidden automatically in fullscreen.
- Configurable mouse wheel behavior: previous/next image or zoom in/out.
- Remembers window placement, viewing mode, information-bar state, wheel mode, and UI language.
- NVIDIA RTX Video SDK 1.1 D3D11 VSR backend.
- Press `R` to toggle RTX VSR. It runs only when enlargement is needed and always uses Ultra quality (quality level 4).
- RTX processing runs on a dedicated background worker. Navigation never waits for AI processing, and an old result cannot replace a newer page.
- The window title shows `[RTX]` after enhancement is applied.
- Hold `C` to compare with the original image; release it to return immediately to the enhanced result.
- VSR output is calculated from the actual display size. A 4K fullscreen view is recalculated toward 3840×2160, with a maximum dimension of 7680 pixels.
- No “processing/applied” overlay appears while changing images. The information bar shows the actual RTX output resolution.
- Press `H` to toggle the VSR Ultra → TrueHDR D3D11 GPU pipeline in the original EinzigViewer main window. It presents directly through 10-bit HDR10/Rec.2020, opens no separate HDR window, and performs no CPU readback between VSR and HDR.
- HDR is presented in a dedicated content surface inside the main window so both information bars remain visible. When switching directly from HDR to VSR, EinzigViewer safely retires the previous TrueHDR task before starting VSR, preventing the display and navigation from sticking on the last HDR frame.
- The integrated presentation verifies that Windows HDR is enabled, uses the display's peak brightness, and provides Standard, Vivid, and Soft presets.
- HDR reuses the main viewer's folder index and eight-image prefetch in each direction. Navigate continuously with the arrow keys, Page Up/Page Down, Space, or the mouse wheel. TrueHDR runs through a background queue that retains only the latest request during rapid navigation, keeping the main window responsive and preventing stale results from replacing the current image.
- Multiple EinzigViewer processes may run at the same time, with an independent NGX instance per process. Two concurrent VSR Ultra → TrueHDR pipelines have been verified locally on one RTX 4070 Ti SUPER; actual capacity and performance depend on GPU load and VRAM.
- Press `M` for maximized-window mode while retaining the title bar, menu, and taskbar. Press `F11` for borderless fullscreen.
- In F11 fullscreen, the cursor hides after three idle seconds only when it remains on EinzigViewer's monitor. Movement, opening the context menu, switching applications, or leaving fullscreen restores it immediately; EinzigViewer never hides a cursor located on another monitor.
- The context menu keeps the “RTX Video Enhancement (VSR + HDR)” toggle separate from the “HDR mode” submenu, which contains only Standard, Vivid, and Gentle.
- Select Traditional Chinese or English from Help → Language. The main window, context menu, notices, and RTX window follow and remember the selection.

## Running EinzigViewer

Download `EinzigViewer-0.7.0-win-x64.zip` from [GitHub Releases](https://github.com/akaJooooooker/EinzigViewer/releases/latest), extract the complete archive, and run `EinzigViewer.exe`. Keep `nvngx_vsr.dll` and `nvngx_truehdr.dll` next to the executable for RTX features. The archive also contains a bilingual `README.txt`.

The locally built Release executable is located at:

```text
out\EinzigViewer.exe
```

Launch it and drag an image into the window, or pass an image path as the first command-line argument:

```powershell
.\out\EinzigViewer.exe "D:\Manga\001.jpg"
```

## Controls

| Input | Action |
|---|---|
| `O` | Open an image or EPUB |
| `V` | Toggle gap-free continuous vertical reading; wheel input scrolls in this mode |
| `Ctrl+S` | Export at the original size as a standard sRGB PNG; HDR JPEG XR is tone-mapped to SDR first |
| `Ctrl+Shift+S` | Enter a target long edge, preserve the aspect ratio, and export a downscaled PNG |
| `←` / `Page Up` / `Backspace` | Previous image |
| `→` / `Page Down` / `Space` | Next image |
| Mouse wheel | Navigate or zoom according to View → Mouse Wheel |
| `Ctrl` + wheel, `+` / `-` | Force cursor-/screen-centered zoom regardless of the wheel setting |
| Left mouse drag | Pan the image |
| `1` | Fit to window |
| `2` | Fit to width |
| `3` | Actual size (100%) |
| `Home` / `End` | First/last image |
| `F11` or double-click | Toggle borderless fullscreen |
| `M` | Toggle maximized-window mode while retaining the title bar, menu, and taskbar |
| `I` | Show/hide the information bar |
| `R` | Enable/disable “RTX VSR Super Resolution” |
| `H` | Toggle “RTX Video Enhancement (VSR + HDR)” in the original main window; arrows/wheel still navigate, `M` maximizes, `F11` enters fullscreen, and RTX → HDR preset selects the intensity |
| Hold `C` | Show the original image temporarily; release to restore the RTX VSR result |
| Right-click | Open the context menu; the VSR + HDR toggle is separate from “HDR mode,” which contains only Standard/Vivid/Gentle |

## JPEG XR and PNG Export

Windows includes a native WIC JPEG XR codec. JXR can contain 8/16/32-bit integer data as well as HDR or wide-gamut fixed-point and floating-point data. EinzigViewer treats extended-RGB JPEG XR as linear scRGB according to WIC rules, uses the 80-nit scRGB reference and a 203-nit SDR paper white with a luminance-preserving ACES shoulder, then performs linear-to-sRGB encoding. The resulting PNG is a standard 8-bit sRGB image intended for browsers, social sites, and ordinary image applications; it is not an HDR archival format.

Downscaling uses the Fant interpolation mode of WIC `IWICBitmapScaler`. The user supplies the target long edge and the other dimension is calculated automatically. Technical references: [WIC JPEG XR/native pixel formats](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-codec-native-pixel-formats) and [WIC bitmap scaling](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-bitmapsources-howto-scale).

## RTX Requirements

- 64-bit Windows 10 or newer.
- GeForce RTX 20 Series or newer, or NVIDIA RTX 1000 Series or newer GPU.
- An NVIDIA driver that supports both the installed GPU and RTX Video SDK. The SDK 1.1 documentation bundled locally lists 550.58 as the minimum driver version.
- EinzigViewer must run on the NVIDIA RTX GPU. On dual-GPU laptops, assign EinzigViewer to the high-performance NVIDIA GPU through Windows Graphics settings or the NVIDIA App.
- VSR requires `nvngx_vsr.dll`; integrated HDR additionally requires `nvngx_truehdr.dll`. The HDR10 swap chain is built directly into `EinzigViewer.exe` in v0.6.0.
- Integrated HDR presentation requires an HDR display with HDR enabled under Windows Settings → System → Display → HDR.

EinzigViewer calls RTX Video SDK directly instead of relying on the browser-oriented driver enhancement toggle. Therefore, when the requirements above are met, the `R`-key RTX feature can work even if “Video Enhancement” is disabled in NVIDIA Control Panel or the NVIDIA App. That setting primarily controls driver enhancement for supported browsers and media players; compatible hardware, drivers, SDK runtime files, and execution on the NVIDIA GPU are still required.

If the computer lacks a compatible RTX GPU, uses an outdated driver, has HDR disabled in Windows or on the display, or is missing a required DLL, EinzigViewer displays an error dialog and returns to normal image viewing in the same main window instead of closing. Both VSR and TrueHDR background work catch processing exceptions; an HDR failure releases the HDR10 swap chain and restores Direct2D.

Official references: [RTX Video SDK Getting Started](https://developer.nvidia.com/rtx-video-sdk/getting-started) and [NVIDIA Control Panel video image settings](https://www.nvidia.com/content/Control-Panel-Help/vLatest/en-us/mergedProjects/Display/Reference_Adjust_Video_Image_Settings.htm).

## Building

Requirements: Visual Studio 2022 with the “Desktop development with C++” workload and a Windows SDK.

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
```

For an RTX-enabled build, extract SDK 1.1 to `.sdk/rtx-video-1.1.0`, or specify its path while configuring:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DEINZIGVIEWER_RTX_SDK_ROOT="D:\SDK\RTX_Video_SDK_v1.1.0"
```

When configuration succeeds, CMake prints `EinzigViewer RTX backend: enabled` and automatically copies the Release `nvngx_vsr.dll` next to the executable. EinzigViewer can still be built as a standard image viewer when the SDK is unavailable.

You may also open the folder or `CMakeLists.txt` directly in Visual Studio.

## Current Limitations

- Continuous reading is SDR; RTX VSR/HDR uses single-page mode. EPUB support displays images, without text/CSS layout, vector-only SVG, or DRM decryption.

- GIF and animated WebP currently display only their first frame.
- WebP, HEIC, AVIF, and JPEG XL support depends on installed Windows WIC codecs.
- HDR JPEG XR → PNG currently uses a fixed SDR tone map; exposure, paper white, and curve controls are not yet exposed. PNG output is always 8-bit sRGB.
- Thumbnail browsing, a folder tree, CBZ, two-page mode, file management, and complete ICC color management are not yet implemented.
- RTX VSR and TrueHDR are connected as GPU textures on the same D3D11 device, and the 10-bit HDR swap chain is now integrated into the original EinzigViewer main window.
- The current VSR result is read back to the CPU before being passed to Direct2D. A future stage will use zero-copy D3D11 texture presentation.
- The first RTX use initializes NGX and may take several seconds; subsequent images are faster.
- VSR results are kept only in memory; there is no disk cache yet.

## NVIDIA SDK Notice

EinzigViewer uses NVIDIA RTX Video SDK. The SDK and runtime are governed by the NVIDIA RTX SDKs License and are not ordinary source assets of this project. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

For additional design and roadmap details, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/RTX_INTEGRATION.md](docs/RTX_INTEGRATION.md).
