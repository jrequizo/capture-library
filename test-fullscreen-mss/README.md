# MSS Performance Spike

This project evaluates the performance of the `python-mss` library by capturing fullscreen screenshots from a specified monitor and displaying real-time performance metrics.

## Overview

The monitor captures video input from monitor index 2 at the fastest possible rate and provides:

- **Current FPS**: Real-time framerate of the most recent capture
- **Average FPS (1m)**: Average framerate over the last 60 seconds
- **Min/Max FPS**: Minimum and maximum captured framerates
- **1% Low FPS**: 1st percentile of framerates (99% of frames exceed this)
- **Live Graph**: Rolling 1-minute window showing 1-second averaged framerates

## Features

- Real-time performance visualization
- Thread-safe capture loop
- Matplotlib-based graphing with rolling window
- Elapsed time tracking
- Support for multiple monitors

## Installation

1. Ensure you have Python 3.7+ installed
2. Install dependencies:
```bash
pip install -r requirements.txt
```

## Usage

Run the performance monitor:

```bash
python performance_monitor.py
```

The GUI window will display:
- Live performance metrics updated every 500ms
- A dynamic graph showing the last 60 seconds of performance
- Monitor information (resolution)
- Elapsed time since capture started

### Configuration

To capture from a different monitor, modify the `monitor_index` parameter in the `main` section of `performance_monitor.py`:

```python
monitor = PerformanceMonitor(monitor_index=1, history_seconds=60)  # Change monitor_index here
```

**Note**: Monitor indices start at 1 (0 is typically the virtual monitor in mss)

## Output

The window displays:
- **Metrics Panel**: Current performance statistics
- **Graph Panel**: 60-second rolling window of 1-second averaged framerates
- **Status Bar**: Connection status and monitor information

## Technical Details

- **Capture Method**: Uses `mss.mss().grab()` for fastest possible screen capture
- **Threading**: Capture runs in a background thread while GUI updates in main thread
- **Timing**: Frame times are measured in milliseconds and converted to FPS
- **Storage**: Rolling buffer stores up to ~6000 frame measurements (100 FPS * 60 seconds)

## Performance Expectations

Expected framerates vary by:
- System load and CPU availability
- Monitor resolution
- Whether Vsync is enabled on the display
- Other applications running concurrently

Typical desktop systems can achieve 30-120+ FPS depending on configuration.
