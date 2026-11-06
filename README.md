# Sparkshot

<img src="assets/Sparkshot.svg" alt="Sparkshot logo" width="64">

**English** · [Русский](README.ru.md)

Capture a region of your screen, mark it up in the editor, and copy it to the clipboard or save it as a PNG.

**Windows x64 · Portable EXE · C++20 / Win32**

[Build Sparkshot](#build-from-source)

## Quick start

1. Build and run `Sparkshot.exe`.
2. Open Sparkshot from the Windows tray and start a capture. In Settings you can choose a capture shortcut, a save folder and the interface language.
3. Drag to select the area. Annotate it, then use the editor controls to copy or save the image.

Choose **English / Русский** in Settings. Sparkshot saves your choice in `data/settings.ini` beside the EXE. On first launch it uses Russian on Russian Windows and English on other systems. The system folder picker depends on Windows language resources.

## Features

- Resize or move the capture region, then add arrows, lines, text, numbers, shapes or pixelation.
- Pick colors from the screenshot with a magnified eyedropper. Undo with **Ctrl + Z** and redo with **Ctrl + Y** or **Ctrl + Shift + Z**.
- Copy the result to the clipboard or save numbered PNG files without overwriting existing files.
- Run from a portable EXE and optionally start with Windows.

## Build from source

Install Visual Studio 2026 Build Tools with MSVC x64, the Windows SDK, CMake and Ninja. The checked-in launcher expects the default Build Tools path.

```cmd
build.cmd
```

Run `.cache/build/Sparkshot.exe`. No installer or separate Visual C++ Runtime is required for the built EXE.

To check the localization resources, build the `SparkshotLocalizationTest` target:

```cmd
build.cmd build-l10n SparkshotLocalizationTest
.cache\build-l10n\SparkshotLocalizationTest.exe
```
