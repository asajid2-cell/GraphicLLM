# Automated Test Recording System

This system automatically records your screen while building, running, and testing your CortexEngine project. Perfect for:
- Verifying bug fixes visually
- Creating test recordings for regression testing
- Documenting issues or features
- Automated visual testing

## Quick Start

### Prerequisites

1. **Install FFmpeg** (required for screen recording):
   ```batch
   winget install ffmpeg
   ```
   Or download from: https://ffmpeg.org/download.html

2. **Verify installation**:
   ```batch
   ffmpeg -version
   ```

### Basic Usage

Simply run:
```batch
test-recording.bat
```

This will:
1. Build your CortexEngine project
2. Start screen recording
3. Launch the engine
4. Execute preset commands/keyboard inputs
5. Record for 60 seconds (configurable)
6. Stop recording and save the video
7. Open the recorded video automatically

## Configuration

Edit [test-config.json](test-config.json) to customize:

```json
{
  "recording": {
    "duration": 60,              // Recording time in seconds
    "framerate": 30,             // FPS (30 recommended for testing)
    "quality": "ultrafast",      // FFmpeg preset: ultrafast, fast, medium, slow
    "output_directory": "test_recordings"
  },
  "build": {
    "skip_build": false,         // Set to true to skip building
    "build_config": "Release"
  },
  "commands": {
    "wait_before_commands": 5,   // Wait time for engine to load
    "key_sequences": [           // Your test commands
      {
        "description": "Test WASD movement",
        "keys": ["w", "a", "s", "d"],
        "delay_between_keys": 500,
        "delay_after_sequence": 1000
      }
    ]
  }
}
```

## Advanced Usage

### Skip Building

If you've already built the project:
```batch
test-recording.bat -SkipBuild
```

Or via PowerShell:
```powershell
.\automate-test.ps1 -SkipBuild
```

### Custom Duration

Record for a different duration:
```powershell
.\automate-test.ps1 -Duration 120
```

### Custom Output Directory

Save recordings to a specific folder:
```powershell
.\automate-test.ps1 -OutputDir "my_tests"
```

## Keyboard Input Commands

You can send any keyboard inputs during testing. Edit the `key_sequences` in [test-config.json](test-config.json).

### Supported Key Formats

- **Letters/Numbers**: `"w"`, `"a"`, `"1"`, `"2"`
- **Special Keys**:
  - `"{ENTER}"` - Enter key
  - `"{ESC}"` - Escape key
  - `"{TAB}"` - Tab key
  - `" "` - Space bar
  - `"{F1}"` through `"{F12}"` - Function keys
  - `"{UP}"`, `"{DOWN}"`, `"{LEFT}"`, `"{RIGHT}"` - Arrow keys
  - `"{BACKSPACE}"`, `"{DELETE}"`
  - `"+{TAB}"` - Shift+Tab
  - `"^c"` - Ctrl+C
  - `"%{F4}"` - Alt+F4

### Example Custom Test Sequence

```json
{
  "description": "Test player movement and menu",
  "keys": ["w", "w", "w", "{SPACE}", "s", "s", "{ESC}"],
  "delay_between_keys": 300,
  "delay_after_sequence": 2000
}
```

## Output Files

Recordings are saved to `test_recordings/` (or your configured directory) with timestamps:
```
test_recordings/
├── test_2026-01-15_14-30-25.mp4
├── test_2026-01-15_15-45-10.mp4
└── test_2026-01-15_16-20-33.mp4
```

## Troubleshooting

### "FFmpeg not found"
- Install FFmpeg: `winget install ffmpeg`
- Or add FFmpeg to your system PATH

### "CortexEngine.exe not found"
- Build the project first: `cd CortexEngine && build.bat`
- Or use `-SkipBuild $false` to force a rebuild

### Recording shows black screen
- FFmpeg might not have permissions to capture screen
- Try running as administrator

### Keyboard inputs not working
- Make sure the CortexEngine window has focus
- Some keys might not work if the engine has custom key handlers
- Try adjusting `wait_before_commands` in config

### Video quality issues
- For better quality, change `"quality": "ultrafast"` to `"medium"` or `"slow"`
- Higher quality = larger files and more CPU usage during recording

## Workflow Examples

### Test a Bug Fix

1. Edit [test-config.json](test-config.json) to reproduce the bug scenario
2. Run `test-recording.bat` to record the current behavior
3. Fix the bug in your code
4. Run `test-recording.bat` again
5. Compare the two videos to verify the fix

### Continuous Testing

Create different config files for different test scenarios:

```powershell
# Test basic movement
.\automate-test.ps1 -ConfigFile "test-movement.json"

# Test rendering features
.\automate-test.ps1 -ConfigFile "test-rendering.json"

# Test UI interactions
.\automate-test.ps1 -ConfigFile "test-ui.json"
```

### Quick Verification

For a quick 30-second test without building:
```powershell
.\automate-test.ps1 -SkipBuild -Duration 30
```

## Tips

- Keep test sequences short and focused on specific features
- Use descriptive config file names for different test scenarios
- Review recordings at 2x speed for faster verification
- Archive important test recordings that show issues or fixes
- Combine with git commits: record before and after major changes

## Comparing Recordings

To compare two test videos side-by-side (great for verifying bug fixes):

```powershell
.\compare-recordings.ps1
```

This will show a list of recordings and let you select two to compare. Or specify them directly:

```powershell
.\compare-recordings.ps1 -Video1 "test_recordings\test_before.mp4" -Video2 "test_recordings\test_after.mp4"
```

The output will be a side-by-side video showing both recordings simultaneously.

## Example Configurations

### Quick Test (30 seconds)

```batch
test-recording.bat -ConfigFile "test-config-quick.json"
```

- Skips build
- 30 seconds duration
- Basic movement test only
- Fast for quick verifications

### Extended Test (2 minutes)

```batch
test-recording.bat -ConfigFile "test-config-extended.json"
```

- Includes build
- 120 seconds duration
- Comprehensive movement, jumping, and UI tests
- Better for full regression testing

## Files

- [test-recording.bat](test-recording.bat) - Quick launcher (batch file)
- [automate-test.ps1](automate-test.ps1) - Main PowerShell script
- [test-config.json](test-config.json) - Default configuration
- [test-config-quick.json](test-config-quick.json) - Quick 30-second test config
- [test-config-extended.json](test-config-extended.json) - Extended 2-minute test config
- [compare-recordings.ps1](compare-recordings.ps1) - Compare two recordings side-by-side
- [run-all-tests.bat](run-all-tests.bat) - Run multiple test configurations in sequence
- `test_recordings/` - Output directory for recordings

## License

Use freely for testing your CortexEngine project.
