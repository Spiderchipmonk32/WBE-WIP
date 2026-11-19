# Worldbuilder (C++)

A detailed 2D world map of a round planet that you can generate and sculpt. The map wraps east to west,
and you can zoom from the whole world down to about 20 m per pixel. Three map styles: Realistic, Fantasy
(parchment) and Elevation.

Built with [raylib](https://www.raylib.com) (graphics) and [raygui](https://github.com/raysan5/raygui) (UI).
Both are downloaded automatically the first time you build, so you don't need to install them.

## One-time setup (Windows)

1. **Install a C++ compiler.** Download *Build Tools for Visual Studio 2022* from
   https://visualstudio.microsoft.com/downloads/ (under "Tools for Visual Studio").
   In the installer, tick **Desktop development with C++** and install. This includes CMake.
   (If you already have Visual Studio 2022 with C++ installed, skip this.)
2. **Install VS Code extensions:** *C/C++* (Microsoft) and *CMake Tools* (Microsoft).
   VS Code will offer them when you open this folder.

## Build and run

1. In VS Code, **File → Open Folder…** and pick this `worldbuilder-cpp` folder.
2. When CMake Tools asks for a **kit**, choose **Visual Studio Build Tools 2022 Release - amd64**
   (or "Visual Studio Community 2022 ... amd64").
3. The first configure downloads raylib and raygui (needs internet, about a minute).
4. Check the status bar at the bottom says **Release**. Debug builds work but generate terrain much more slowly.
5. Press **F7** to build, then **Shift+F5** to run, or click the ▶ button in the status bar.

If anything goes wrong, open the Command Palette (Ctrl+Shift+P) and run **CMake: Delete Cache and Reconfigure**.

## Controls

| Action | Control |
| --- | --- |
| Zoom (towards the cursor) | Mouse wheel |
| Move the map | Right-drag, middle-drag, WASD, or left-drag with the Pan tool |
| Switch tool | 1 Pan · 2 Raise · 3 Lower · 4 Smooth · 5 Flatten |
| Brush size | `[` and `]`, or the slider |
| Sculpt | Left-drag with a brush tool |
| Undo | Ctrl+Z |

## How it works

| File | What it does |
| --- | --- |
| `src/planet.*` | Generates the planet. Elevation is a function of position, so it can be sampled at any detail. Brush strokes are stored as edit layers on top. |
| `src/tilemap.*` | The 2D map: 256x256 tiles at 12 zoom levels, drawn on background threads and cached; renders the three styles. |
| `src/noise.h` | Seeded 3D simplex noise. |
| `src/main.cpp` | Window, map navigation, sculpting, grid, scale bar and UI. |

## Getting a ready-to-run .exe

**Built on your PC:** after building once in VS Code (Release), the app is at
`build/Release/Worldbuilder.exe`. It's self-contained, so you can copy it anywhere (Desktop, USB stick)
and double-click it. You don't need VS Code to run it.

**Built by GitHub (nothing to install):** put this folder in a GitHub repository. The workflow in
`.github/workflows/build.yml` builds the exe automatically. Open the repo's **Actions** tab, click the
latest run, and download **Worldbuilder-windows** under Artifacts.
