# Eidolyx

[繁體中文](README.md) | **English**

Eidolyx is a native Windows image viewer designed for enlarging comic text, linework, and general images, with integrated NVIDIA RTX Video VSR and HDR. VSR is super-resolution enhancement, not content restoration: it cannot reconstruct details that do not exist in the source, but it is often especially helpful for small text, screentones, and edges in low-resolution comics. After opening an image, Eidolyx automatically indexes the same folder, applies natural filename sorting, and pre-decodes the eight neighboring images in each direction.

Current version: **0.5.0 with integrated RTX Video VSR/HDR presentation in the main window**.

> **Project rename:** Starting with 0.5.0, the project is renamed from MiraView to **Eidolyx** to avoid an existing project with the same name. Eidolyx is a coined name derived from the Greek *eidolon* (image) and Latin *lux* (light). On its first launch, the new version reads the legacy MiraView settings when no new settings file exists; after exit, settings are saved to `%LOCALAPPDATA%\Eidolyx\settings.ini`.

## Implemented Features

- Native C++20/Win32 application with no .NET Runtime requirement.
- WIC decoding for JPG, PNG, BMP, the first frame of GIF, TIFF, ICO, JPEG XR, and formats such as WebP, HEIC, and AVIF when a corresponding system WIC codec is installed.
- EXIF Orientation support for automatic JPEG rotation.
- Automatic same-folder indexing after opening one image.
- Windows logical natural sorting: `1, 2, 3, 10` instead of `1, 10, 2, 3`.
- Four background image-decoding workers.
- Direction-aware prefetching of eight images before and after the current image, prioritizing the next image.
- 768 MB LRU memory cache with the current and neighboring images pinned.
- Obsolete queued work is discarded during rapid navigation, keeping the current image at the highest priority.
- Direct2D rendering, fit-to-window, fit-to-width, 100% view, mouse panning, and cursor-centered zooming.
- Native Windows menu bar (File, Navigate, View, RTX, and Help), context menu, and drag-and-drop opening.
- The main window initially opens centered in the current monitor's work area at 960×640, then remembers user-adjusted position and size.
- Dedicated Eidolyx application icon embedded in the executable and title bar.
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
- Press `H` to toggle the VSR Ultra → TrueHDR D3D11 GPU pipeline in the original Eidolyx main window. It presents directly through 10-bit HDR10/Rec.2020, opens no separate HDR window, and performs no CPU readback between VSR and HDR.
- HDR is presented in a dedicated content surface inside the main window so both information bars remain visible. When switching directly from HDR to VSR, Eidolyx safely retires the previous TrueHDR task before starting VSR, preventing the display and navigation from sticking on the last HDR frame.
- The integrated presentation verifies that Windows HDR is enabled, uses the display's peak brightness, and provides Standard, Vivid, and Soft presets.
- HDR reuses the main viewer's folder index and eight-image prefetch in each direction. Navigate continuously with the arrow keys, Page Up/Page Down, Space, or the mouse wheel. TrueHDR runs through a background queue that retains only the latest request during rapid navigation, keeping the main window responsive and preventing stale results from replacing the current image.
- Multiple Eidolyx processes may run at the same time, with an independent NGX instance per process. Two concurrent VSR Ultra → TrueHDR pipelines have been verified locally on one RTX 4070 Ti SUPER; actual capacity and performance depend on GPU load and VRAM.
- Press `M` for maximized-window mode while retaining the title bar, menu, and taskbar. Press `F11` for borderless fullscreen.
- In F11 fullscreen, the cursor hides after three idle seconds only when it remains on Eidolyx's monitor. Movement, opening the context menu, switching applications, or leaving fullscreen restores it immediately; Eidolyx never hides a cursor located on another monitor.
- The context menu keeps the “RTX Video Enhancement (VSR + HDR)” toggle separate from the “HDR mode” submenu, which contains only Standard, Vivid, and Gentle.
- Select Traditional Chinese or English from Help → Language. The main window, context menu, notices, and RTX window follow and remember the selection.

## Running Eidolyx

Download `Eidolyx-0.5.0-win-x64.zip` from [GitHub Releases](https://github.com/akaJooooooker/Eidolyx/releases/latest), extract the complete archive, and run `Eidolyx.exe`. Keep `nvngx_vsr.dll` and `nvngx_truehdr.dll` next to the executable for RTX features; v0.5.0 does not require a separate HDR helper executable. The archive also contains a bilingual `README.txt`.

The locally built Release executable is located at:

```text
out\Eidolyx.exe
```

Launch it and drag an image into the window, or pass an image path as the first command-line argument:

```powershell
.\out\Eidolyx.exe "D:\Manga\001.jpg"
```

## Controls

| Input | Action |
|---|---|
| `O` | Open an image |
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

## RTX Requirements

- 64-bit Windows 10 or newer.
- GeForce RTX 20 Series or newer, or NVIDIA RTX 1000 Series or newer GPU.
- An NVIDIA driver that supports both the installed GPU and RTX Video SDK. The SDK 1.1 documentation bundled locally lists 550.58 as the minimum driver version.
- Eidolyx must run on the NVIDIA RTX GPU. On dual-GPU laptops, assign Eidolyx to the high-performance NVIDIA GPU through Windows Graphics settings or the NVIDIA App.
- VSR requires `nvngx_vsr.dll`; integrated HDR additionally requires `nvngx_truehdr.dll`. The HDR10 swap chain is built directly into `Eidolyx.exe` in v0.5.0.
- Integrated HDR presentation requires an HDR display with HDR enabled under Windows Settings → System → Display → HDR.

Eidolyx calls RTX Video SDK directly instead of relying on the browser-oriented driver enhancement toggle. Therefore, when the requirements above are met, the `R`-key RTX feature can work even if “Video Enhancement” is disabled in NVIDIA Control Panel or the NVIDIA App. That setting primarily controls driver enhancement for supported browsers and media players; compatible hardware, drivers, SDK runtime files, and execution on the NVIDIA GPU are still required.

If the computer lacks a compatible RTX GPU, uses an outdated driver, has HDR disabled in Windows or on the display, or is missing a required DLL, Eidolyx displays an error dialog and returns to normal image viewing in the same main window instead of closing. Both VSR and TrueHDR background work catch processing exceptions; an HDR failure releases the HDR10 swap chain and restores Direct2D.

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
  -DEIDOLYX_RTX_SDK_ROOT="D:\SDK\RTX_Video_SDK_v1.1.0"
```

When configuration succeeds, CMake prints `Eidolyx RTX backend: enabled` and automatically copies the Release `nvngx_vsr.dll` next to the executable. Eidolyx can still be built as a standard image viewer when the SDK is unavailable.

You may also open the folder or `CMakeLists.txt` directly in Visual Studio.

## Current Limitations

- GIF and animated WebP currently display only their first frame.
- WebP, HEIC, AVIF, and JPEG XL support depends on installed Windows WIC codecs.
- Thumbnail browsing, a folder tree, CBZ, two-page mode, file management, and complete ICC color management are not yet implemented.
- RTX VSR and TrueHDR are connected as GPU textures on the same D3D11 device, and the 10-bit HDR swap chain is now integrated into the original Eidolyx main window.
- The current VSR result is read back to the CPU before being passed to Direct2D. A future stage will use zero-copy D3D11 texture presentation.
- The first RTX use initializes NGX and may take several seconds; subsequent images are faster.
- VSR results are kept only in memory; there is no disk cache yet.

## NVIDIA SDK Notice

Eidolyx uses NVIDIA RTX Video SDK. The SDK and runtime are governed by the NVIDIA RTX SDKs License and are not ordinary source assets of this project. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

For additional design and roadmap details, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/RTX_INTEGRATION.md](docs/RTX_INTEGRATION.md).
