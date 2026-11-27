#!/usr/bin/env python3
"""Single source of truth for the Widget Library.
Run:  python3 tools/library_spec.py     -> writes library/<id>/<id>.ini and src/library/Catalog.cpp
Skin text may use @ACCENT@ and @ALPHA@: they are replaced with each widget's default accent/opacity so
the skin's own defaults always agree with the default option labels shown in the UI."""
import os, textwrap

ACCENTS = "Blue=80,170,255,255|Violet=150,120,255,255|Teal=0,210,190,255|Rose=255,100,150,255|Amber=255,190,60,255|Green=110,220,130,255"
OPACITY = "Clear=0|Glass=70|Low=110|Medium=170|High=225|Solid=255"
def table(s): return dict(p.split("=", 1) for p in s.split("|"))
ACC, OPA = table(ACCENTS), table(OPACITY)

CATS = ["SystemMonitoring", "Productivity", "Weather", "Music", "Visualizers", "Appearance", "Utilities"]
ILLUSTRO = "Inspired by illustro, the default skin suite that ships with Rainmeter. Independent DeskSkin-native recreation."
def inspired(n): return f"Inspired by {n}, a community Rainmeter suite. Independent DeskSkin-native widget; not affiliated with the original author."

W = []
def widget(id, name, cat, desc, source, ini, accent="Blue", alpha="High", requires="", options=(), feature=False):
    W.append(dict(id=id, name=name, cat=cat, desc=desc, source=source, ini=ini, accent=accent, alpha=alpha,
                  requires=requires, options=list(options), feature=feature))

HDR = """[Variables]
Accent=@ACCENT@
BgAlpha=@ALPHA@
Muted=160,172,190,255
Track=255,255,255,40
"""

# ------------------------------------------------------------------ illustro
widget("illustro-clock", "Clock", "Productivity",
 "Digital time with the full date underneath. Follows the 12/24-hour and Date switches on the Widgets page.", ILLUSTRO,
 HDR + """
[Skin]
X=40
Y=40
Width=240
Height=124
Update=1000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=10

[Time]
Meter=String
X=0
Y=10
W=240
H=70
FontFace=Segoe UI Light
FontSize=44
FontColor=255,255,255,255
Align=Center
Text=%HM%

[Date]
Meter=String
X=0
Y=82
W=240
H=26
FontSize=15
FontColor=#Muted#
Align=Center
Text=%DATELONG%

[Line]
Meter=Bar
X=50
Y=114
W=140
H=2
Value=1
BarColor=#Accent#
SolidColor=#Accent#
""")

widget("illustro-system", "System", "SystemMonitoring",
 "Live CPU load with a history graph, plus RAM in use out of total.", ILLUSTRO,
 HDR + """
[Skin]
X=40
Y=180
Width=240
Height=140
Update=1000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=10

[CpuLabel]
Meter=String
X=14
Y=8
W=100
H=20
FontSize=13
FontWeight=SemiBold
FontColor=255,255,255,255
Text=CPU

[CpuValue]
Meter=String
X=110
Y=8
W=116
H=20
FontSize=13
FontColor=#Accent#
Align=Right
Text=%CPU%%

[CpuBar]
Meter=Bar
X=14
Y=32
W=212
H=5
Source=CPU
BarColor=#Accent#
SolidColor=#Track#

[CpuGraph]
Meter=Histogram
X=14
Y=42
W=212
H=36
Source=CPU
Max=100
BarColor=#Accent#

[RamLabel]
Meter=String
X=14
Y=88
W=60
H=20
FontSize=13
FontWeight=SemiBold
FontColor=255,255,255,255
Text=RAM

[RamValue]
Meter=String
X=70
Y=88
W=156
H=20
FontSize=13
FontColor=#Accent#
Align=Right
Text=%RAM_USED% / %RAM_TOTAL%

[RamBar]
Meter=Bar
X=14
Y=112
W=212
H=5
Source=RAM
BarColor=#Accent#
SolidColor=#Track#
""")

widget("illustro-disk1", "Disk (1 Disk)", "SystemMonitoring",
 "Space used, free and total for one drive. Change the drive letter in the options.", ILLUSTRO,
 HDR + """Drive=C

[Skin]
X=40
Y=336
Width=240
Height=92
Update=5000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=10

[Label]
Meter=String
X=14
Y=8
W=120
H=22
FontSize=14
FontWeight=SemiBold
FontColor=255,255,255,255
Text=Disk #Drive#:

[Percent]
Meter=String
X=110
Y=8
W=116
H=22
FontSize=14
FontColor=#Accent#
Align=Right
Text=%DISK:#Drive#%% used

[Bar]
Meter=Bar
X=14
Y=36
W=212
H=8
Source=DISK:#Drive#
BarColor=#Accent#
SolidColor=#Track#

[Free]
Meter=String
X=14
Y=54
W=212
H=22
FontSize=12
FontColor=#Muted#
Text=%DISK_FREE:#Drive#% free of %DISK_TOTAL:#Drive#%
""", options=[("Drive", "Drive letter", "", "C", False)])

widget("illustro-disk2", "Disk (2 Disks)", "SystemMonitoring",
 "Two drives side by side with used-space bars. Pick both drive letters in the options.", ILLUSTRO,
 HDR + """Drive1=C
Drive2=D

[Skin]
X=40
Y=444
Width=240
Height=140
Update=5000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=10

[L1]
Meter=String
X=14
Y=8
W=100
H=20
FontSize=13
FontWeight=SemiBold
FontColor=255,255,255,255
Text=Disk #Drive1#:

[P1]
Meter=String
X=110
Y=8
W=116
H=20
FontSize=13
FontColor=#Accent#
Align=Right
Text=%DISK:#Drive1#%%

[B1]
Meter=Bar
X=14
Y=32
W=212
H=7
Source=DISK:#Drive1#
BarColor=#Accent#
SolidColor=#Track#

[F1]
Meter=String
X=14
Y=44
W=212
H=20
FontSize=12
FontColor=#Muted#
Text=%DISK_FREE:#Drive1#% free

[L2]
Meter=String
X=14
Y=72
W=100
H=20
FontSize=13
FontWeight=SemiBold
FontColor=255,255,255,255
Text=Disk #Drive2#:

[P2]
Meter=String
X=110
Y=72
W=116
H=20
FontSize=13
FontColor=#Accent#
Align=Right
Text=%DISK:#Drive2#%%

[B2]
Meter=Bar
X=14
Y=96
W=212
H=7
Source=DISK:#Drive2#
BarColor=#Accent#
SolidColor=#Track#

[F2]
Meter=String
X=14
Y=108
W=212
H=20
FontSize=12
FontColor=#Muted#
Text=%DISK_FREE:#Drive2#% free
""", options=[("Drive1", "First drive", "", "C", False), ("Drive2", "Second drive", "", "D", False)])

widget("illustro-network", "Network", "SystemMonitoring",
 "Download and upload speed with live graphs, measured on your real network adapters.", ILLUSTRO,
 HDR + """
[Skin]
X=296
Y=40
Width=240
Height=170
Update=1000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=10

[DownLabel]
Meter=String
X=14
Y=8
W=100
H=20
FontSize=13
FontWeight=SemiBold
FontColor=255,255,255,255
Text=Download

[DownValue]
Meter=String
X=100
Y=8
W=126
H=20
FontSize=13
FontColor=#Accent#
Align=Right
Text=%NET_DOWN%

[DownGraph]
Meter=Histogram
X=14
Y=32
W=212
H=44
Source=NET_DOWN
Max=0
BarColor=#Accent#

[UpLabel]
Meter=String
X=14
Y=88
W=100
H=20
FontSize=13
FontWeight=SemiBold
FontColor=255,255,255,255
Text=Upload

[UpValue]
Meter=String
X=100
Y=88
W=126
H=20
FontSize=13
FontColor=#Accent#
Align=Right
Text=%NET_UP%

[UpGraph]
Meter=Histogram
X=14
Y=112
W=212
H=44
Source=NET_UP
Max=0
BarColor=#Accent#
""")

widget("illustro-google", "Google", "Utilities",
 "A search box on your desktop: click it, type, press Enter and your browser opens the results. Choose Google, Bing or DuckDuckGo.", ILLUSTRO,
 HDR + """EngineUrl=https://www.google.com/search?q=

[Skin]
X=296
Y=226
Width=280
Height=56
Update=60000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=28

[Box]
Meter=Bar
X=10
Y=10
W=260
H=36
Value=1
BarColor=255,255,255,235
SolidColor=255,255,255,235

[Mark]
Meter=Bar
X=10
Y=10
W=6
H=36
Value=1
BarColor=#Accent#
SolidColor=#Accent#

[Prompt]
Meter=String
X=30
Y=15
W=232
H=26
FontSize=15
FontColor=90,96,110,255
Text=Search the web...
LeftMouseUpAction=Cmd:Search:#EngineUrl#
""", requires="A web browser",
 options=[("EngineUrl", "Search engine", "Google=https://www.google.com/search?q=|Bing=https://www.bing.com/search?q=|DuckDuckGo=https://duckduckgo.com/?q=", "Google", False)])

widget("illustro-feeds", "Feeds", "Productivity",
 "The latest headlines from any RSS or Atom feed. Click a headline to open it. Set the feed address in the options.", ILLUSTRO,
 HDR + """
[Skin]
X=296
Y=298
Width=340
Height=190
Update=30000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=10

[Title]
Meter=String
X=14
Y=8
W=312
H=22
FontSize=14
FontWeight=SemiBold
FontColor=#Accent#
Text=%FEED_NAME%
""" + "".join(f"""
[Item{i}]
Meter=String
X=14
Y={34 + (i - 1) * 25}
W=312
H=22
FontSize=12
FontColor=255,255,255,255
Text=%FEED_{i}%
LeftMouseUpAction=Cmd:OpenFeed:{i}
""" for i in range(1, 7)), requires="Internet access (the feed's website)",
 options=[("shared.feedUrl", "Feed URL", "", "https://feeds.bbci.co.uk/news/rss.xml", True)])

widget("illustro-recyclebin", "Recycle Bin", "Utilities",
 "Shows how many items and how much space the Recycle Bin holds. Click to open it, or use the button to empty it.", ILLUSTRO,
 HDR + """
[Skin]
X=296
Y=504
Width=220
Height=112
Update=5000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=10

[Title]
Meter=String
X=14
Y=8
W=190
H=22
FontSize=14
FontWeight=SemiBold
FontColor=255,255,255,255
Text=Recycle Bin
LeftMouseUpAction=Open:shell:RecycleBinFolder

[Count]
Meter=String
X=14
Y=34
W=190
H=20
FontSize=13
FontColor=#Accent#
Text=%RECYCLE_COUNT% items, %RECYCLE_SIZE%
LeftMouseUpAction=Open:shell:RecycleBinFolder

[Button]
Meter=Bar
X=14
Y=66
W=192
H=30
Value=1
BarColor=#Accent#
SolidColor=#Accent#

[ButtonText]
Meter=String
X=14
Y=71
W=192
H=22
FontSize=13
FontWeight=SemiBold
FontColor=20,22,28,255
Align=Center
Text=Empty Recycle Bin
LeftMouseUpAction=Cmd:EmptyRecycleBin
""")

widget("illustro-welcome", "Welcome", "Utilities",
 "A friendly greeting with your user name, computer name and how long Windows has been running.", ILLUSTRO,
 HDR + """
[Skin]
X=40
Y=600
Width=300
Height=104
Update=30000
BackgroundColor=20,22,28,#BgAlpha#
CornerRadius=10

[Greeting]
Meter=String
X=16
Y=10
W=270
H=22
FontSize=14
FontColor=#Muted#
Text=%GREETING%,

[User]
Meter=String
X=16
Y=30
W=270
H=40
FontFace=Segoe UI Light
FontSize=28
FontColor=255,255,255,255
Text=%USER%

[Info]
Meter=String
X=16
Y=72
W=270
H=22
FontSize=12
FontColor=#Accent#
Text=%HOST%  |  up %UPTIME%
""")

# ------------------------------------------------------------------ Elegance
widget("elegance-clock", "Elegance Clock", "Appearance",
 "A tall, minimalist clock with no background: hours and minutes in thin type with the weekday and date below.", inspired("Elegance2"),
 HDR + """
[Skin]
X=600
Y=40
Width=340
Height=160
Update=1000
BackgroundColor=0,0,0,#BgAlpha#
CornerRadius=18

[Time]
Meter=String
X=0
Y=0
W=340
H=100
FontFace=Segoe UI Light
FontSize=76
FontColor=255,255,255,255
Align=Center
Text=%HOUR%:%MIN%

[Day]
Meter=String
X=0
Y=100
W=340
H=26
FontSize=16
FontWeight=SemiBold
FontColor=#Accent#
Align=Center
Text=%WEEKDAY%

[Date]
Meter=String
X=0
Y=126
W=340
H=22
FontSize=13
FontColor=255,255,255,190
Align=Center
Text=%MONTH% %DAY%, %YEAR%
""", alpha="Clear")

widget("elegance-system", "Elegance System", "SystemMonitoring",
 "Minimalist system monitor: thin bars for CPU, memory and your system drive on a transparent background.", inspired("Elegance2"),
 HDR + """
[Skin]
X=600
Y=216
Width=260
Height=132
Update=1000
BackgroundColor=0,0,0,#BgAlpha#
CornerRadius=14

[CpuL]
Meter=String
X=8
Y=6
W=120
H=20
FontSize=12
FontColor=255,255,255,200
Text=CPU
[CpuV]
Meter=String
X=130
Y=6
W=122
H=20
FontSize=12
FontColor=255,255,255,255
Align=Right
Text=%CPU%%
[CpuB]
Meter=Bar
X=8
Y=28
W=244
H=2
Source=CPU
BarColor=#Accent#
SolidColor=#Track#

[RamL]
Meter=String
X=8
Y=44
W=120
H=20
FontSize=12
FontColor=255,255,255,200
Text=Memory
[RamV]
Meter=String
X=130
Y=44
W=122
H=20
FontSize=12
FontColor=255,255,255,255
Align=Right
Text=%RAM%%
[RamB]
Meter=Bar
X=8
Y=66
W=244
H=2
Source=RAM
BarColor=#Accent#
SolidColor=#Track#

[DiskL]
Meter=String
X=8
Y=82
W=120
H=20
FontSize=12
FontColor=255,255,255,200
Text=Disk C:
[DiskV]
Meter=String
X=130
Y=82
W=122
H=20
FontSize=12
FontColor=255,255,255,255
Align=Right
Text=%DISK:C%%
[DiskB]
Meter=Bar
X=8
Y=104
W=244
H=2
Source=DISK:C
BarColor=#Accent#
SolidColor=#Track#
""", alpha="Clear")

# ------------------------------------------------------------------ Weather
widget("astro-weather", "Astro Weather", "Weather",
 "Current conditions and a 3-day forecast with temperature highs and lows. Uses your location, or the city you set.", inspired("Astro Weather"),
 HDR + """
[Skin]
X=600
Y=364
Width=300
Height=210
Update=30000
BackgroundColor=14,18,34,#BgAlpha#
CornerRadius=16

[Icon]
Meter=String
X=14
Y=8
W=84
H=84
FontFace=Segoe UI Symbol
FontSize=58
FontColor=#Accent#
Align=Center
Text=%WEATHER_ICON%

[Temp]
Meter=String
X=100
Y=8
W=190
H=64
FontFace=Segoe UI Light
FontSize=52
FontColor=255,255,255,255
Text=%WEATHER_TEMP%

[Cond]
Meter=String
X=104
Y=72
W=190
H=22
FontSize=14
FontColor=255,255,255,230
Text=%WEATHER_COND%

[City]
Meter=String
X=16
Y=98
W=270
H=20
FontSize=12
FontColor=#Muted#
Text=%WEATHER_CITY%  |  H %WEATHER_HI%  L %WEATHER_LO%  |  Wind %WEATHER_WIND%
""" + "".join(f"""
[D{i}Name]
Meter=String
X={8 + (i - 1) * 96}
Y=128
W=92
H=20
FontSize=12
FontWeight=SemiBold
FontColor=#Accent#
Align=Center
Text=%WEATHER_D{i}_NAME%
[D{i}Icon]
Meter=String
X={8 + (i - 1) * 96}
Y=146
W=92
H=30
FontFace=Segoe UI Symbol
FontSize=22
FontColor=255,255,255,255
Align=Center
Text=%WEATHER_D{i}_ICON%
[D{i}Temp]
Meter=String
X={8 + (i - 1) * 96}
Y=178
W=92
H=20
FontSize=12
FontColor=#Muted#
Align=Center
Text=%WEATHER_D{i}_HI% / %WEATHER_D{i}_LO%
""" for i in range(1, 4)), accent="Amber", alpha="High", requires="Internet access (Open-Meteo, no key needed)",
 options=[("shared.city", "City (blank = auto)", "", "", True), ("shared.units", "Units", "Metric=metric|Imperial=imperial", "Metric", True)])

widget("jacket", "Do I Need a Jacket?", "Weather",
 "One clear answer: yes or no, with the reason (cold, rain, snow, wind or storms) based on the feels-like temperature.", inspired("Do I Need a Jacket?"),
 HDR + """
[Skin]
X=600
Y=590
Width=300
Height=116
Update=30000
BackgroundColor=14,18,34,#BgAlpha#
CornerRadius=16

[Answer]
Meter=String
X=16
Y=12
W=270
H=40
FontFace=Segoe UI Semibold
FontSize=24
FontColor=#Accent#
Text=%JACKET%

[Why]
Meter=String
X=16
Y=54
W=270
H=22
FontSize=14
FontColor=255,255,255,230
Text=%JACKET_WHY%

[Now]
Meter=String
X=16
Y=82
W=270
H=22
FontSize=12
FontColor=#Muted#
Text=%WEATHER_CITY%: %WEATHER_TEMP%, %WEATHER_COND%
""", accent="Teal", requires="Internet access (Open-Meteo, no key needed)",
 options=[("shared.city", "City (blank = auto)", "", "", True), ("shared.units", "Units", "Metric=metric|Imperial=imperial", "Metric", True)])

# ------------------------------------------------------------------ Music
CONTROLS = lambda y, x0: f"""
[Prev]
Meter=String
X={x0}
Y={y}
W=44
H=30
FontFace=Segoe UI Symbol
FontSize=18
FontColor=255,255,255,255
Align=Center
Text=\u23EE
LeftMouseUpAction=Cmd:MediaPrev

[Play]
Meter=String
X={x0 + 46}
Y={y}
W=44
H=30
FontFace=Segoe UI Symbol
FontSize=20
FontColor=#Accent#
Align=Center
Text=\u23EF
LeftMouseUpAction=Cmd:MediaPlayPause

[Next]
Meter=String
X={x0 + 92}
Y={y}
W=44
H=30
FontFace=Segoe UI Symbol
FontSize=18
FontColor=255,255,255,255
Align=Center
Text=\u23ED
LeftMouseUpAction=Cmd:MediaNext
"""
MUSIC_REQ = "Spotify desktop app (no account or API key needed)"

widget("rainify", "Rainify", "Music",
 "Now-playing card for Spotify with previous / play-pause / next buttons. Reads the Spotify desktop app locally; no login or API key.", inspired("Rainify"),
 HDR + """
[Skin]
X=960
Y=40
Width=300
Height=124
Update=1000
BackgroundColor=18,24,22,#BgAlpha#
CornerRadius=16

[Status]
Meter=String
X=16
Y=10
W=268
H=18
FontSize=11
FontWeight=SemiBold
FontColor=#Accent#
Text=%MEDIA_STATUS%

[Title]
Meter=String
X=16
Y=28
W=268
H=26
FontSize=17
FontWeight=SemiBold
FontColor=255,255,255,255
Text=%MEDIA_TITLE%

[Artist]
Meter=String
X=16
Y=54
W=268
H=22
FontSize=13
FontColor=#Muted#
Text=%MEDIA_ARTIST%
""" + CONTROLS(84, 82), accent="Green", requires=MUSIC_REQ)

widget("mond", "Mond", "Music",
 "A modern clock with your current song and music controls underneath.", inspired("Mond"),
 HDR + """
[Skin]
X=960
Y=180
Width=320
Height=180
Update=1000
BackgroundColor=16,16,26,#BgAlpha#
CornerRadius=20

[Time]
Meter=String
X=18
Y=6
W=284
H=74
FontFace=Segoe UI Light
FontSize=56
FontColor=255,255,255,255
Text=%HM%

[Date]
Meter=String
X=20
Y=76
W=284
H=22
FontSize=14
FontColor=#Accent#
Text=%DATELONG%

[Line]
Meter=Bar
X=18
Y=106
W=284
H=1
Value=1
BarColor=255,255,255,50
SolidColor=255,255,255,50

[Song]
Meter=String
X=18
Y=114
W=284
H=22
FontSize=13
FontColor=255,255,255,230
Text=%MEDIA_LINE%
""" + CONTROLS(140, 92), accent="Violet", requires=MUSIC_REQ)

widget("cleartext", "Cleartext", "Music",
 "Just the song and artist, in clean type on your desktop. Click it to play or pause.", inspired("Cleartext"),
 HDR + """
[Skin]
X=960
Y=376
Width=420
Height=64
Update=1000
BackgroundColor=0,0,0,#BgAlpha#
CornerRadius=12

[Line]
Meter=String
X=10
Y=6
W=400
H=34
FontFace=Segoe UI Semibold
FontSize=22
FontColor=255,255,255,255
Text=%MEDIA_LINE%
LeftMouseUpAction=Cmd:MediaPlayPause

[Status]
Meter=String
X=12
Y=40
W=400
H=18
FontSize=11
FontColor=#Accent#
Text=%MEDIA_STATUS%  |  click to play / pause
""", alpha="Clear", requires=MUSIC_REQ)

# ------------------------------------------------------------------ Visualizers
VREQ = "An audio output device (visualizes whatever is playing)"
widget("monstercat-visualizer", "Monstercat Visualizer", "Visualizers",
 "Clean vertical spectrum bars that react to whatever your PC is playing.", inspired("Monstercat Visualizer"),
 HDR + """
[Skin]
X=960
Y=456
Width=440
Height=130
Update=33
BackgroundColor=0,0,0,#BgAlpha#
CornerRadius=12

[Spectrum]
Meter=Bars
X=10
Y=8
W=420
H=114
Bands=48
Style=Bars
BarColor=#Accent#

[Status]
Meter=String
X=12
Y=4
W=410
H=18
FontSize=11
FontColor=255,255,255,160
Text=%AUDIO_STATUS%
""", accent="Teal", alpha="Clear", requires=VREQ)

widget("fountain-of-colors", "Fountain of Colors", "Visualizers",
 "A mirrored rainbow spectrum that fountains up and down from the centre line with the music.", inspired("Fountain of Colors"),
 HDR + """Rainbow=1

[Skin]
X=960
Y=602
Width=440
Height=150
Update=33
BackgroundColor=8,8,16,#BgAlpha#
CornerRadius=14

[Spectrum]
Meter=Bars
X=14
Y=10
W=412
H=130
Bands=44
Style=Mirror
Rainbow=#Rainbow#
BarColor=#Accent#

[Status]
Meter=String
X=14
Y=4
W=410
H=18
FontSize=11
FontColor=255,255,255,160
Text=%AUDIO_STATUS%
""", alpha="Low", requires=VREQ, options=[("Rainbow", "Colors", "Rainbow=1|Single color=0", "Rainbow", False)])

widget("visbubble", "VisBubble", "Visualizers",
 "Colourful bubbles that swell and shrink to the beat, one per frequency band.", inspired("VisBubble"),
 HDR + """Rainbow=1

[Skin]
X=960
Y=768
Width=440
Height=130
Update=33
BackgroundColor=8,10,20,#BgAlpha#
CornerRadius=14

[Bubbles]
Meter=Bars
X=14
Y=10
W=412
H=110
Bands=28
Style=Bubbles
Rainbow=#Rainbow#
BarColor=#Accent#

[Status]
Meter=String
X=14
Y=4
W=410
H=18
FontSize=11
FontColor=255,255,255,160
Text=%AUDIO_STATUS%
""", alpha="Low", requires=VREQ, options=[("Rainbow", "Colors", "Rainbow=1|Single color=0", "Rainbow", False)])

# ------------------------------------------------------------------ Utilities
def hexbtn(name, label, x, y, action, hot=False):
    return f"""
[{name}]
Meter=Hex
X={x}
Y={y}
W=88
H=76
FontSize=13
FontWeight=SemiBold
FontColor=255,255,255,255
SolidColor={'#Accent#' if hot else '34,42,62,235'}
BorderColor=#Accent#
Text={label}
LeftMouseUpAction={action}
"""
widget("honeycomb", "Honeycomb", "Utilities",
 "A hexagonal app launcher: seven honeycomb buttons for your browser, files, notepad, calculator, settings, terminal and store.", inspired("Honeycomb"),
 HDR + """
[Skin]
X=1300
Y=40
Width=300
Height=290
Update=60000
BackgroundColor=0,0,0,#BgAlpha#
CornerRadius=16
""" + hexbtn("Center", "Web", 106, 106, "Open:https://www.google.com", True)
    + hexbtn("Top", "Files", 106, 26, "Open:shell:MyComputerFolder")
    + hexbtn("Bottom", "Notes", 106, 187, "Open:notepad.exe")
    + hexbtn("UL", "Calc", 35.5, 66, "Open:calc.exe")
    + hexbtn("UR", "Settings", 176.5, 66, "Open:ms-settings:")
    + hexbtn("LL", "Terminal", 35.5, 147, "Run:cmd.exe|")
    + hexbtn("LR", "Store", 176.5, 147, "Open:ms-windows-store:"), alpha="Clear", requires="")

widget("droptop-four", "Droptop Four", "Utilities",
 "A slim bar for the top of your screen: quick launchers on the left, the clock in the middle, live system numbers on the right.", inspired("Droptop Four"),
 HDR + """
[Skin]
X=0
Y=0
Width=1100
Height=36
Update=1000
BackgroundColor=14,16,24,#BgAlpha#
CornerRadius=0

[Brand]
Meter=String
X=14
Y=6
W=90
H=24
FontSize=14
FontWeight=Bold
FontColor=#Accent#
Text=DeskSkin
""" + "".join(f"""
[Btn{n}]
Meter=String
X={110 + i * 80}
Y=6
W=76
H=24
FontSize=13
FontColor=255,255,255,230
Text={label}
LeftMouseUpAction={act}
""" for i, (n, label, act) in enumerate([("A", "Files", "Open:shell:MyComputerFolder"), ("B", "Web", "Open:https://www.google.com"), ("C", "Settings", "Open:ms-settings:")])) + """
[Clock]
Meter=String
X=400
Y=4
W=300
H=28
FontSize=15
FontWeight=SemiBold
FontColor=255,255,255,255
Align=Center
Text=%WEEKDAY_SHORT% %HM%

[Stats]
Meter=String
X=640
Y=6
W=450
H=24
FontSize=12
FontColor=#Muted#
Align=Right
Text=CPU %CPU%%   RAM %RAM%%   Down %NET_DOWN%   Up %NET_UP%
""", alpha="Solid")

widget("jaxcore", "JaxCore", "Utilities",
 "Modular system controls: live CPU/RAM bars plus buttons for lock, task manager, volume, mute, settings and the Run box.", inspired("JaxCore"),
 HDR + """
[Skin]
X=1300
Y=346
Width=260
Height=246
Update=1000
BackgroundColor=18,20,30,#BgAlpha#
CornerRadius=14

[Title]
Meter=String
X=14
Y=8
W=232
H=22
FontSize=14
FontWeight=Bold
FontColor=#Accent#
Text=JaxCore

[CpuB]
Meter=Bar
X=14
Y=36
W=232
H=5
Source=CPU
BarColor=#Accent#
SolidColor=#Track#
[CpuT]
Meter=String
X=14
Y=44
W=232
H=18
FontSize=11
FontColor=#Muted#
Text=CPU %CPU%%   RAM %RAM%%
[RamB]
Meter=Bar
X=14
Y=64
W=232
H=5
Source=RAM
BarColor=#Accent#
SolidColor=#Track#
""" + "".join(f"""
[B{n}]
Meter=Bar
X={14 + (i % 2) * 118}
Y={82 + (i // 2) * 52}
W=114
H=46
Value=1
BarColor=40,46,66,255
SolidColor=40,46,66,255
[T{n}]
Meter=String
X={14 + (i % 2) * 118}
Y={82 + (i // 2) * 52 + 12}
W=114
H=24
FontSize=13
FontWeight=SemiBold
FontColor=255,255,255,255
Align=Center
Text={label}
LeftMouseUpAction={act}
""" for i, (n, label, act) in enumerate([
    (1, "Lock", "Run:rundll32.exe|user32.dll,LockWorkStation"), (2, "Task Manager", "Open:taskmgr.exe"),
    (3, "Mute", "Cmd:VolumeMute"), (4, "Run...", "Run:explorer.exe|shell:::{2559a1f3-21d7-11d4-bdaf-00c04f60b9f0}"),
    (5, "Volume -", "Cmd:VolumeDown"), (6, "Volume +", "Cmd:VolumeUp")])),
 requires="")

# ------------------------------------------------------------------ Productivity
widget("enigma-calendar", "Enigma Calendar", "Productivity",
 "A month calendar with today highlighted. Updates itself at midnight.", inspired("Enigma"),
 HDR + """
[Skin]
X=1300
Y=608
Width=260
Height=256
Update=60000
BackgroundColor=20,20,30,#BgAlpha#
CornerRadius=14

[Month]
Meter=String
X=14
Y=8
W=232
H=26
FontSize=16
FontWeight=SemiBold
FontColor=255,255,255,255
Text=%MONTH% %YEAR%

[Grid]
Meter=Calendar
X=10
Y=40
W=240
H=204
FontSize=12
FontColor=255,255,255,235
SolidColor=#Accent#
WeekStart=0
""")

widget("enigma-notes", "Enigma Notes", "Productivity",
 "A sticky note on your desktop. Click it to edit notes.txt in Notepad; it refreshes as soon as you save.", inspired("Enigma"),
 HDR + """
[Skin]
X=1620
Y=40
Width=280
Height=190
Update=2000
BackgroundColor=52,46,22,#BgAlpha#
CornerRadius=10

[Hit]
Meter=Bar
X=0
Y=0
W=280
H=190
Value=0
SolidColor=0,0,0,0
LeftMouseUpAction=Cmd:EditNotes

[Title]
Meter=String
X=14
Y=8
W=250
H=22
FontSize=14
FontWeight=Bold
FontColor=#Accent#
Text=Notes
""" + "".join(f"""
[N{i}]
Meter=String
X=14
Y={34 + (i - 1) * 25}
W=252
H=22
FontSize=13
FontColor=255,255,255,240
Text=%NOTE_{i}%
""" for i in range(1, 7)), accent="Amber")

widget("enigma-feeds", "Enigma Feeds", "Productivity",
 "A compact headline ticker for your RSS feed with an accent stripe. Click a headline to read it.", inspired("Enigma"),
 HDR + """
[Skin]
X=1620
Y=246
Width=360
Height=150
Update=30000
BackgroundColor=20,20,30,#BgAlpha#
CornerRadius=10

[Stripe]
Meter=Bar
X=0
Y=0
W=5
H=150
Value=1
BarColor=#Accent#
SolidColor=#Accent#

[Title]
Meter=String
X=16
Y=6
W=336
H=22
FontSize=13
FontWeight=Bold
FontColor=#Accent#
Text=%FEED_NAME%
""" + "".join(f"""
[H{i}]
Meter=String
X=16
Y={30 + (i - 1) * 23}
W=336
H=21
FontSize=12
FontColor=255,255,255,235
Text=%FEED_{i}%
LeftMouseUpAction=Cmd:OpenFeed:{i}
""" for i in range(1, 5)), accent="Rose", requires="Internet access (the feed's website)",
 options=[("shared.feedUrl", "Feed URL", "", "https://feeds.bbci.co.uk/news/rss.xml", True)])

widget("simple-clean", "Simple Clean", "SystemMonitoring",
 "One lightweight card with the time, the date, today's weather and CPU / RAM bars.", inspired("Simple Clean"),
 HDR + """
[Skin]
X=1620
Y=412
Width=280
Height=206
Update=1000
BackgroundColor=24,26,34,#BgAlpha#
CornerRadius=12

[Time]
Meter=String
X=16
Y=6
W=250
H=60
FontFace=Segoe UI Light
FontSize=44
FontColor=255,255,255,255
Text=%HM%

[Date]
Meter=String
X=18
Y=64
W=250
H=22
FontSize=13
FontColor=#Muted#
Text=%DATELONG%

[Weather]
Meter=String
X=18
Y=92
W=250
H=24
FontSize=14
FontColor=255,255,255,235
Text=%WEATHER_TEMP%  %WEATHER_COND%

[CpuB]
Meter=Bar
X=18
Y=134
W=244
H=5
Source=CPU
BarColor=#Accent#
SolidColor=#Track#
[CpuT]
Meter=String
X=18
Y=142
W=244
H=18
FontSize=11
FontColor=#Muted#
Text=CPU %CPU%%
[RamB]
Meter=Bar
X=18
Y=168
W=244
H=5
Source=RAM
BarColor=#Accent#
SolidColor=#Track#
[RamT]
Meter=String
X=18
Y=176
W=244
H=18
FontSize=11
FontColor=#Muted#
Text=RAM %RAM_USED% of %RAM_TOTAL%
""", requires="Internet access for the weather line only (Open-Meteo)",
 options=[("shared.city", "City (blank = auto)", "", "", True), ("shared.units", "Units", "Metric=metric|Imperial=imperial", "Metric", True)])

# ------------------------------------------------------------------ Appearance feature
widget("translucent-taskbar", "Translucent Taskbar", "Appearance",
 "Makes the Windows taskbar clear, blurred or acrylic. Experimental: it relies on an undocumented Windows call that can behave differently between Windows 11 builds. Turn it off to restore the normal taskbar.",
 "Inspired by TranslucentTB, an open-source taskbar tool. Independent DeskSkin implementation; not affiliated with it.", "", feature=True, requires="Windows taskbar (experimental)",
 options=[("style", "Style", "Clear=clear|Blur=blur|Acrylic=acrylic", "Blur", False),
          ("opacity", "Tint strength", "None=0|Light=50|Medium=110|Strong=170|Heavy=220", "Light", False)])

# ===================================================================== generation
def esc(s): return s.replace("\\", "\\\\").replace('"', '\\"')

def cpp_string(s): return '"' + esc(s) + '"'

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = ['// GENERATED by tools/library_spec.py - do not edit by hand.', '#include "library/Catalog.h"', '', 'namespace deskskin::library {', 'namespace {', '']
    for w in W:
        text = w["ini"].replace("@ACCENT@", ACC[w["accent"]]).replace("@ALPHA@", OPA[w["alpha"]])
        if not w["feature"]:
            d = os.path.join(root, "library", w["id"])
            os.makedirs(d, exist_ok=True)
            open(os.path.join(d, w["id"] + ".ini"), "w", encoding="utf-8", newline="\n").write(text.lstrip("\n"))
        var = w["id"].replace("-", "_")
        out.append(f'const char kIni_{var}[] = R"INI({text.lstrip(chr(10))})INI";')
    out += ['', '}  // namespace', '', 'const std::vector<WidgetDef>& All() {', '  static const std::vector<WidgetDef> kAll = {']
    for w in W:
        var = w["id"].replace("-", "_")
        opts = []
        if not w["feature"]:
            opts.append(("Accent", "Accent color", ACCENTS, w["accent"], False))
            opts.append(("BgAlpha", "Background", OPACITY, w["alpha"], False))
        opts += [tuple(o) for o in w["options"]]
        o_txt = ", ".join(f'{{{cpp_string(k)}, {cpp_string(l)}, {cpp_string(c)}, {cpp_string(d)}, {"true" if s else "false"}}}' for k, l, c, d, s in opts)
        out.append(f'    {{{cpp_string(w["id"])}, {cpp_string(w["name"])}, Category::{w["cat"]}, {cpp_string(w["desc"])}, "DeskSkin", {cpp_string(w["source"])}, '
                   f'{cpp_string(w["requires"])}, {"true" if w["feature"] else "false"}, {"nullptr" if w["feature"] else "kIni_" + var}, {{{o_txt}}}}},')
    out += ['  };', '  return kAll;', '}', '', '}  // namespace deskskin::library', '']
    open(os.path.join(root, "src", "library", "Catalog.cpp"), "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print(f"{len(W)} widgets ({sum(1 for w in W if not w['feature'])} skins + {sum(1 for w in W if w['feature'])} feature)")

if __name__ == "__main__":
    main()
