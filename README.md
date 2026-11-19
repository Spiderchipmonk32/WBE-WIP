# Worldbuilder (C++)

A zoomable 3D planet you can generate and sculpt, from orbit down to about 1 m of detail.
This is step 1b of the worldbuilding app: the globe, level-of-detail terrain and sculpting.

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
| Zoom | Mouse wheel |
| Move across the planet | WASD, middle-drag, or left-drag with the Orbit tool |
| Turn and tilt the view | Right-drag (Q / E also turn) |
| Switch tool | 1 Orbit · 2 Raise · 3 Lower · 4 Smooth · 5 Flatten |
| Brush size | `[` and `]`, or the slider |
| Sculpt | Left-drag with a brush tool |
| Undo | Ctrl+Z |

Tip: tilt the view toward the horizon (right-drag) and set height exaggeration to 1× when you're close
to the ground, to see the terrain at its real scale.

## How it works

| File | What it does |
| --- | --- |
| `src/planet.*` | Generates the planet. Elevation is a function of position, so it can be sampled at any detail. Brush strokes are stored as edit layers on top. |
| `src/terrain.*` | The level-of-detail system: a cube projected onto a sphere, each face a quadtree of 33×33 tiles that split as you get closer (19 levels). |
| `src/noise.h` | Seeded 3D simplex noise. |
| `src/main.cpp` | Window, camera, sculpting input, shaders and UI. |

The world is drawn relative to the camera and positions are kept in double precision, so the image doesn't jitter at street level.

## Getting a ready-to-run .exe

**Built on your PC:** after building once in VS Code (Release), the app is at
`build/Release/Worldbuilder.exe`. It's self-contained, so you can copy it anywhere (Desktop, USB stick)
and double-click it. You don't need VS Code to run it.

**Built by GitHub (nothing to install):** put this folder in a GitHub repository. The workflow in
`.github/workflows/build.yml` builds the exe automatically. Open the repo's **Actions** tab, click the
latest run, and download **Worldbuilder-windows** under Artifacts.
