DeskSkin - desktop widgets for the Windows 11 wallpaper layer
==============================================================

Run:   double-click DeskSkin.exe. A demo clock appears on your desktop and a tray icon appears
       near the clock. Nothing is installed; everything stays in this folder.

Use:   Drag a widget with the left mouse button. Right-click it for: Enabled, Lock position,
       Click-through, Edit skin file, Reset position. Right-click (or click) the tray icon for:
       Refresh, Skins, Reset all positions, Start with Windows, Open skins folder, Exit.
       Positions and options are saved automatically in settings.ini.

Widget Library: click "Widget Library" in the sidebar. 26 ready-made widgets (illustro Clock/System/Disk/
       Network/Google/Feeds/Recycle Bin/Welcome, plus independent DeskSkin versions inspired by popular
       community suites) and the experimental Translucent Taskbar. Search, filter by category, favorite,
       preview live, Install/Uninstall, enable/disable, change colors and options. "Import skin..." adds
       your own DeskSkin skins (.ini or .zip). Everything is built in: installing needs no download.
       Online widgets (weather, feeds) only contact their service (Open-Meteo, your feed's site, ipwho.is
       for auto-location) once installed. Now-playing reads the Spotify desktop app; visualizers listen to
       your PC's audio output. No accounts or API keys are used.

Skins: Skins\<Name>\<anything>.ini - each .ini is one skin. Saving a skin file reloads it
       automatically. See Skins\Demo\Demo.ini for the syntax (String and Bar meters,
       #Variables#, LeftMouseUpAction=Open:<target>). Only install skins you trust: a skin can
       open files, folders and links.

Files: DeskSkin.log - diagnostics (attach/creation results). Please include it in bug reports.
       settings.ini - saved positions/options.
       If this folder is read-only (e.g. Program Files), data goes to %APPDATA%\DeskSkin instead.

Autostart: tray menu > "Start with Windows" (adds/removes a per-user Run entry; no admin needed).
Uninstall: Exit from the tray, untick "Start with Windows", delete the folder.
