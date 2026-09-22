# LocaleBridge

[繁體中文](README.md) · **English**

**Run any Windows application in a locale you choose** (ANSI code page / LCID) without
changing the system-wide "Language for non-Unicode programs". Useful for showing Simplified
Chinese, Japanese and other legacy ANSI-encoded apps (games, old software) correctly on a
Traditional Chinese or English Windows.

Same idea as [Locale Emulator](https://github.com/xupefei/Locale-Emulator) /
[Locale_Remulator](https://github.com/InWILL/Locale_Remulator), but:

- Native **x64 and Windows 11** support, injecting with **Microsoft Detours**.
- **Right-click any `.exe`** and start it with a locale profile (Explorer integration).
- A clean **settings GUI** for profiles and the context menu, itself available in
  **繁體中文 / English**.
- Handles the "launcher + protected game binary" two-stage pattern: it deliberately
  **does not hook the window-class functions**, so a launcher can still detect the game
  window (the reason some locale emulators leave the launcher stuck on "game not started").

> This is an encoding-compatibility tool: it only changes how bytes are interpreted.
> No file of the target program is modified.

---

## Components

| File | Purpose |
|------|---------|
| `LocaleBridge.exe` | Settings GUI (profiles, context menu, manual launch) |
| `LocaleLoader64.exe` / `LocaleLoader32.exe` | Launcher: starts the target with a profile and injects the hook |
| `LocaleHook64.dll` / `LocaleHook32.dll` | The injected hook that does the locale emulation |

All four files (plus the GUI's .NET files) must live in the **same folder**.

---

## Usage

### 1. Settings GUI
Run `LocaleBridge.exe`:

- **Locale profiles** — add / edit / delete profiles (name + code page + LCID). Built-in
  presets: 简体中文 (936), 繁體中文 (950), 日本語 (932), 한국어 (949).
- **Context menu** — click "Install / update context menu"; afterwards right-clicking any
  `.exe` shows **"Launch with LocaleBridge" → (your profiles)**.
  (On Windows 11 the classic menu is under "Show more options".)
- **Launch now** — pick a profile, browse for a program, run it.
- **UI language** — the button in the top-right toggles between **繁體中文 / English**
  (stored in `HKCU\Software\LocaleBridge\Language`; on first run it follows your system
  UI language).

### 2. Right-click launch
After installing the context menu: right-click an `.exe` in Explorer → Launch with
LocaleBridge → pick a locale.

### 3. Command line
```
LocaleLoader64.exe --profile "简体中文 (GBK)" "D:\game\game.exe"
LocaleLoader64.exe "D:\game\game.exe"        # no profile → default 936 / zh-CN
```
The loader has `requireAdministrator`, so targets that need elevation trigger UAC.

---

## How it works

```
LocaleLoader ──(Detours: DetourCreateProcess/UpdateProcessWithDll)──▶ target.exe (LocaleHook injected)
   The profile's CodePage/LCID are passed as environment variables; child processes inherit them
        Inside the target, LocaleHook redirects:
          GetACP / GetLocaleInfo …          → report the configured code page / LCID
          MultiByteToWideChar(CP_ACP,…)     → convert using the configured code page (fixes most text)
          CreateFontA / CreateFontIndirect… → force the matching font charset (GDI decodes DBCS by it)
          CreateProcess(A/W/InternalW)      → propagate injection to children (rundll32 helper across bitness)
```

- Injection works by **rewriting the import table with Detours**, so the DLL loads during the
  target's normal initialization — indistinguishable from a normal module to anti-tamper shells.
- Cross-bitness cases (an x86 launcher spawning an x64 game) go through the Detours helper.
- `RegisterClass` / `FindWindow` are **deliberately not hooked**, so the window class name stays
  identical on both the registering and the searching side and launchers still find the game window.

---

## Building

Requires Windows + Visual Studio (MSVC with C++ and the .NET 8 SDK) + CMake ≥ 3.21.

```powershell
# Native parts (build x64 and x86 separately)
cmake -B build-x64 -A x64  ; cmake --build build-x64 --config Release
cmake -B build-x86 -A Win32; cmake --build build-x86 --config Release
# Settings GUI (self-contained: users need no .NET runtime)
dotnet publish gui/LocaleBridge.csproj -c Release -r win-x64 --self-contained -o publish_gui
```
Copy the four native files from `build-*/Release` into `publish_gui`; that folder is the
distributable.

**No local toolchain needed**: the repo ships a GitHub Actions workflow — push, then download
the `LocaleBridge` artifact from the Actions tab (GUI + runtime + the four native files).

---

## Translating the UI

Every UI string lives in one table in [`gui/Lang.cs`](gui/Lang.cs); each entry is an array
ordered like `Lang.Codes`. **To add a language**: add a column to `Codes` / `Names` and one
translation at the same position in each entry. No .resx files, no satellite assemblies.

---

## Dependencies & license

- [Microsoft Detours](https://github.com/microsoft/Detours) (MIT) — injection and hooking.
- The settings GUI is .NET 8 WPF, published self-contained.

MIT licensed.
