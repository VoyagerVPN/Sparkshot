# Sparkshot

<img src="assets/Sparkshot.svg" alt="Sparkshot logo" width="64">

**English** · [Русский](README.ru.md)

Portable screenshot and annotation tool for Windows x64.

[Build Sparkshot](#build-from-source)

## Quick start

1. Run `Sparkshot.exe` and start a capture from its icon in the Windows notification area.
2. Drag to select an area. Use the editor toolbar to add annotations, then choose **Copy** or **Save**.

Set a capture shortcut or choose **English / Русский** in Settings.

## Features

- Arrows, text, numbered markers and pixelation.
- Color picker with a magnifier, plus undo and redo.
- Numbered PNG files without overwriting earlier captures.

## Build from source

Install Visual Studio 2026 Build Tools in the default location with MSVC x64, Windows SDK, CMake and Ninja.

```cmd
build.cmd
```

Run `.cache/build/Sparkshot.exe`.
