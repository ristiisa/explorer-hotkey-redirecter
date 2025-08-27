# Hotkey Blocker

A Windows utility that blocks configurable system hotkeys globally, except when specific target applications are in focus.

## Features

- Block system hotkeys (Win+C, Win+V, etc.) from all applications
- Allow hotkeys for specific apps (like Parsec, remote desktop clients)
- Live configuration reloading - edit config while running
- Silent background operation - no visible window by default
- Debug mode for troubleshooting
- Case-insensitive configuration

## Quick Start

1. **Download** the latest `blocker.exe` from releases
2. **Create** a `config.txt` file (see configuration below)
3. **Run** `blocker.exe` - it runs silently in the background
4. **Edit** `config.txt` anytime to change settings (auto-reloads)

## Configuration

Create a `config.txt` file in the same directory as `blocker.exe`:

```txt
# Which executables can receive the blocked hotkeys (separated by ;)
target=parsecd.exe;parsec.exe;notepad.exe

# Hotkeys to block (one per line)
Win+C
Win+V
Win+F
Win+Z
Win+Y
Win+S

# More examples:
# Win+L
# Ctrl+Shift+Esc  
# Alt+F4
# Ctrl+Alt+Del
```

### Supported Modifiers
- `Win` - Windows key (left or right)
- `Ctrl` - Control key (left or right)  
- `Alt` - Alt key (left or right)
- `Shift` - Shift key (left or right)

You can combine modifiers: `Ctrl+Shift+Alt+F12`

### Supported Keys
- **Letters**: A-Z
- **Numbers**: 0-9  
- **Function keys**: F1-F24

## Command Line Options

```bash
blocker.exe              # Run silently in background
blocker.exe --debug      # Run with console logging
blocker.exe --kill       # Kill all running blocker instances
```

## How It Works

1. **Registers global hotkeys** using Windows `RegisterHotKey` API
2. **Monitors foreground window** to detect active application
3. **Blocks hotkeys** for all apps except those in the `target=` list
4. **Forwards hotkeys** to target apps using low-level input simulation
5. **Watches config file** and reloads settings automatically

## Use Cases

- **Remote desktop** - Block Windows hotkeys except for Parsec/RDP clients
- **Gaming** - Prevent accidental Windows shortcuts during games
- **Kiosks** - Disable system hotkeys on public computers  
- **Focus apps** - Allow shortcuts only in specific applications

## Building

Requires Visual Studio or Windows SDK:

```bash
cl /EHsc /O2 /W4 /DUNICODE /D_UNICODE blocker.cpp user32.lib shell32.lib /link /SUBSYSTEM:WINDOWS
```

## Troubleshooting

### Program not working?
- Run with `--debug` to see what's happening
- Check if hotkeys are already registered by other programs
- Try running as administrator

### Config not reloading?
- Make sure `config.txt` is in the same directory as `blocker.exe`
- Check for syntax errors in the config file
- Watch for popup messages indicating reload status

### Hotkeys still getting through?
- Verify the target application name matches exactly (case-insensitive)
- Some applications may register hotkeys at a lower level
- Try running blocker before starting the target application

## License

MIT License - see LICENSE file for details

## Contributing

Issues and pull requests welcome on GitHub!