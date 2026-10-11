[Watch on YouTube](https://www.youtube.com/watch?v=26LTCoVe8RA)



# CS2-Lan-ESP

External ESP, Aimbot, and RCS for Counter-Strike 2 using NeacSafe64 kernel driver.

## Features

- **Box ESP** - Enemy bounding boxes
- **Health Bars** - Visual health indicators
- **Bone ESP** - Skeleton rendering
- **Name ESP** - Player names
- **Distance** - Distance to enemies
- **Head Circles** - Head position indicators
- **Aimbot** - Smooth aim assist with configurable FOV
- **RCS** - Recoil Control System

## Requirements

- Windows 10/11 (Memory Integrity/VBS must be OFF)
- Administrator privileges
- CS2 running

## Quick Start

### USB Deployment

Run the deployment script to set up a USB drive:

```
Deploy-USB.bat
```

This will:
1. Show available USB drives
2. Let you select one
3. Copy all files with proper hidden attributes
4. The USB is ready to use on any target system

### On Target System

1. Plug in the USB
2. Run `Setup.bat` as Administrator (first time only - installs auto-launch)
3. From then on, USB auto-launches when plugged in
4. Press **END** to exit and clean up

## Configuration

Edit `config.ini` on the USB (in `.data` hidden folder) - changes apply within 2 seconds without restart.

```ini
[ESP]
enabled=1       ; Master ESP toggle
box=1           ; Box ESP
health=1        ; Health bars
bones=1         ; Skeleton
name=1          ; Player names
distance=1      ; Distance display
head=1          ; Head circles
visible_only=0  ; Only show visible enemies

[Aimbot]
enabled=1       ; Master aimbot toggle
fov=5.0         ; FOV radius (degrees)
smooth=3.0      ; Smoothing factor (higher = slower)
bone=6          ; Target bone (6=head, 5=neck, 4=chest)
key=1           ; Aim key (1=left mouse button)

[RCS]
enabled=1       ; Recoil control toggle
scale_x=1.0     ; Horizontal compensation (0.5-1.5)
scale_y=1.0     ; Vertical compensation (0.5-1.5)
start_bullet=1  ; Start after N bullets fired
```

## Architecture

```
USB Drive (visible):
  Setup.bat           <- Only visible file, runs everything

USB Drive (hidden .data folder):
  USB-Lan-Monitor.exe <- Driver lifecycle manager
  USB-Lan-ESP.exe     <- Main ESP (driver embedded)
  Driver.sys          <- NeacSafe64 kernel driver
  config.ini          <- Live-editable settings
```

**Two-process design:**
- **Monitor** - Loads driver, launches ESP, watches for exit/crash, cleans up
- **ESP** - Main overlay with ESP/Aimbot/RCS

On exit (END key) or crash:
1. ESP process terminates
2. Monitor unloads driver
3. Monitor deletes registry keys
4. Temp files auto-delete

## Hotkeys

| Key | Action |
|-----|--------|
| END | Clean exit - unloads driver, removes all traces |
| Left Mouse | Aimbot (hold to aim) |

## Troubleshooting

**"Driver connection failed"**
- Run as Administrator
- Disable Memory Integrity in Windows Security → Device Security → Core Isolation

**"Failed to attach to CS2"**
- CS2 must be running before launching

**No ESP visible**
- Check `config.ini` settings
- Verify CS2 is in borderless/windowed mode

**Aimbot not working**
- Check `[Aimbot] enabled=1` in config.ini
- Aimbot activates when holding left mouse button

## Building from Source

```bash
cd CS2-Lan-ESP
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Outputs in `build/Release/`:
- `USB-Lan-ESP.exe`
- `USB-Lan-Monitor.exe`

## Uninstalling Auto-Launch

On target system, run as Administrator:
```
schtasks /delete /tn "SvcHost" /f
rd /s /q "%LOCALAPPDATA%\Microsoft\Services"
```

-----

# CS2-Lan-ESP: Complete Beginner's Build Guide

## What You Need to Install

You need **one thing**: Visual Studio 2022 (free version is fine).

---

## Step 1: Download Visual Studio 2022

1. Open your web browser
2. Go to: `https://visualstudio.microsoft.com/downloads/`
3. Under "Community 2022" (the free one), click **Free download**
4. A file called `VisualStudioSetup.exe` will download
5. Open your Downloads folder and double-click it

---

## Step 2: Install Visual Studio with C++ Tools

1. The installer will open - let it load
2. You'll see a screen with checkboxes for "Workloads"
3. **Check the box** next to: `Desktop development with C++`
4. Click the **Install** button in the bottom right
5. Wait for it to finish (this can take 15-30 minutes)
6. When done, you can close the installer

---

## Step 3: Get the Project Files

**If you downloaded a ZIP file:**
1. Right-click the ZIP file
2. Click "Extract All..."
3. Choose where to put it (like your Desktop)
4. Click "Extract"

**If you're cloning from GitHub:**
1. Open Visual Studio 2022
2. Click "Clone a repository"
3. Paste the repository URL
4. Click Clone

---

## Step 4: Open the Project

1. Open Visual Studio 2022
2. Click **Open a local folder**
3. Navigate to where you extracted/cloned the project
4. Select the `CS2-Lan-ESP` folder (the one containing `CMakeLists.txt`)
5. Click **Select Folder**

---

## Step 5: Wait for CMake to Configure

1. Visual Studio will automatically detect it's a CMake project
2. Look at the **Output** panel at the bottom
3. Wait until you see: `CMake generation finished`
4. This usually takes 1-2 minutes the first time

---

## Step 6: Build the Project

1. At the top menu, find the dropdown that says **Debug** - click it
2. Change it to **Release**
3. Go to the top menu: **Build** → **Build All**
4. Wait for the build to complete
5. Look at the Output panel - you should see `Build All succeeded`

---

## Step 7: Find Your Built Files

Your compiled programs are now in:
CS2-Lan-ESP/build/Release/

Inside that folder you'll find:
- `USB-Lan-ESP.exe` - The main ESP program
- `USB-Lan-Monitor.exe` - The driver manager

---

## Troubleshooting

**"CMake generation failed"**
- Make sure you installed "Desktop development with C++" in Step 2
- Try: Build menu → Delete Cache and Reconfigure

**"Build failed"**
- Make sure you switched from Debug to Release
- Make sure you're on Windows 10 or 11

**Can't find the Release folder**
- Check inside the `build` folder in your project
- Make sure you selected "Release" not "Debug"

---

## Quick Reference (After First Setup)

Once everything is installed, building again is simple:
1. Open Visual Studio 2022
2. Open the project folder
3. Set to **Release**
4. **Build** → **Build All**
5. Find output in `build/Release/`

Note: Discord has a 2000 character limit per message, so you'll need to split this into multiple messages. I'd suggest breaking it at each --- divider.
