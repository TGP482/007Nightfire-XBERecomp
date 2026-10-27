# 007: Nightfire - XBE Recomp
An unofficial native PC port of the original Xbox version of 007: Nightfire, with extended graphics options, keyboard and mouse support, and other enhancements.

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build.

## Installation
The latest version of [007Nightfire-XBERecomp](https://github.com/TGP482/007Nightfire-XBERecomp/releases) can be found in the Releases page.

- Place your game files in the `assets` folder. This can be an ISO, or the extracted data files.
- Run `nightfire.exe`. The first start compiles the game's code for your PC, this only happens once.
- Recomp Settings can be adjusted in the in-game options menu while the game is running, or through the Settings overlay, which is opened with <kbd>F10</kbd> by default.

## Features
### Display
- **Monitor** - Picks the monitor the game shows on, for Fullscreen and Borderless (and where Windowed opens). Lists every connected monitor by its Windows number and desktop resolution.
- **Display Mode** - Switch between Fullscreen, Borderless and Windowed.
- **Resolution** - Lists every mode your monitor reports and defaults to your desktop resolution, replacing the original 640x480 resolution.
- **Render Resolution** - Renders the game at 20% to 200% of the output resolution, allowing supersampling or downscaling independent of the window size.
- **Field of View** - Sets the horizontal field of view during gameplay from 45 to 140 degrees, with 75 being the game's default. Cutscenes, other cameras and the zoom keep the original.
- **Viewmodel Field of View** - Adjusts the first-person weapon and arms field of view, from 45 to 140 degrees, with 75 being the game's default.
- **Max Frame Rate** - Caps the frame rate to your monitor's refresh rate or a custom value, or removes the cap entirely, replacing the Xbox's 30 FPS limiter.
- **V-Sync** - Reduces screen tearing.
- **HDR Output** - Enables HDR10 output on displays with Windows HDR turned on, detected automatically. Highlights extend up to the display's peak brightness.
- **FPS Counter** - Displays the frame rate on screen.
- **FMV Scale** - Choose between Fill, which makes the FMVs fill widescreen displays, and 4:3, which shows them in a 4:3 frame as on the original Xbox.
- **Colour Blind Mode** - Adjusts the colours for Protanopia, Deuteranopia or Tritanopia.

### Graphics
- **Ambient Occlusion** - Adds screen space ambient occlusion, creating soft contact shadows where surfaces meet. Choose between SSAO and GTAO.
- **Multisample Antialiasing (MSAA)** - Enables 2x, 4x or 8x MSAA to smooth jagged edges while preserving a sharp image. MSAA does not smooth alpha-tested edges such as fences and foliage.
- **Subpixel Morphological Antialiasing (SMAA)** - Enables SMAA to smooth jagged edges across the whole frame with a softer image. SMAA also smooths alpha-tested edges such as fences and foliage, and can be combined with MSAA.
- **Anisotropic Filtering** - Forces the selected anisotropic filtering level on all textures.

### Textures
- **Texture Replacement** - Replaces the game's textures with the `.dds` or `.png` images in the `textures\replacement` folder beside the exe.
- **Texture Dumping** - Saves every texture the game uses into the `textures\dumps` folder beside the exe as you play.
- **Dump Format** - Choose between PNG or DDS, which keeps the original compression and mipmaps.

### Input
- **Mouse and Keyboard** - Full keyboard and mouse support with raw input.
- **Controller** - Controller with SDL3 support.
- **Gyro Aiming** - Adds gyro aiming for controllers with motion sensors, such as the DualSense, DualShock 4 and Switch Pro Controller. Works together with the right stick.

### General
- **Skip Intro** - Skips the intro logos and the intro movie.
- **Pause On Focus Loss** - Pauses the game and its sound while the window is in the background or minimized.
- **Audio Output** - Choose between Automatic, which follows your speakers, Mono, 2.0 Stereo and 5.1 Surround. 5.1 Surround places sounds behind you on the rear speakers.

### Miscellaneous
- **Boot Splash** - Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.
- **Custom Menu Cursor** - Place a `cursor.png` (or `cursor.dds`) beside the exe to replace the Windows arrow in the game's menus.

## System Requirements
### Minimum
- **OS**
  - Windows 10 64-bit
- **CPU** with 2 cores and support for SSE4.2 and POPCNT
  - Intel Nehalem (Intel Core 1st Generation)
  - AMD Bulldozer (AMD FX series)
- **GPU** with support for Direct3D 12
  - NVIDIA GeForce GT 630 (Kepler)
  - AMD Radeon HD 7750
  - Intel HD Graphics 510 (Skylake)
- **Memory**
  - 4 GB
- **Storage**
  - 3 GB for the game files, the compiled game code, saves and shader cache

## Building
### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).
```
git clone https://github.com/TGP482/007Nightfire-XBERecomp.git
```

### 2. Add the Required Game Files
Place your extracted game data files in the `assets` folder. `default.xbe` must be there, with `Driving.xbe` and the `driving` and `eurocom` folders beside it.

> [!NOTE]
> Building does not support the ISO format, so the files must be extracted. ISOs only work for playing.

### 3. Install Dependencies
Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) and select the following **Workloads** and **Individual components**:
- Desktop development with C++
- C++ Clang tools for Windows
- C++ CMake tools for Windows

> [!TIP]
> `build.bat` offers to install the Clang and CMake components for you if they're missing.

### 4. Build the Project
- Run `build.bat`. The first build recompiles the entire game and can take 30 to 90 minutes on weaker systems.
- When it finishes, run `out\build\release\nightfire.exe`.
