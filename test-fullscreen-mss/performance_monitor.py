"""
Capture Library Performance Monitor Spike

This tool evaluates the performance of the custom capture library bindings
by capturing fullscreen screenshots and displaying real-time performance
metrics including current FPS, average FPS, min/max FPS, 1% lows, and a
graph of framerate over the last minute.
"""

import tkinter as tk
from tkinter import ttk
import threading
import time
import argparse
import sys
import gc
import ctypes
from pathlib import Path


def _try_import_capture_module():
    try:
        import capture as capture_module  # pyright: ignore[reportMissingImports]
        return capture_module
    except ImportError:
        # Probe common local build output folders for the pybind module.
        this_file = Path(__file__).resolve()
        workspace_root = this_file.parent.parent
        candidate_dirs = [
            workspace_root / "capture-library" / "build" / "Release",
            workspace_root / "capture-library" / "build",
        ]

        for candidate in candidate_dirs:
            if not candidate.exists():
                continue
            if any(candidate.glob("capture*.pyd")) or any(candidate.glob("capture*.so")):
                sys.path.insert(0, str(candidate))
                try:
                    import capture as capture_module  # pyright: ignore[reportMissingImports]
                    return capture_module
                except ImportError:
                    continue

        return None


try:
    capture = _try_import_capture_module()
except Exception:
    capture = None
import numpy as np
from collections import deque
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import traceback


class PerformanceMonitor:
    def __init__(
        self,
        monitor_index=0,
        history_seconds=60,
        backend="auto",
        timeout_ms=3000,
        dip_threshold_ms=16.7,
        idle_sleep_ms=0.0,
        crossing_correlation_ms=250.0,
    ):
        """
        Initialize the performance monitor.
        
        Args:
            monitor_index: Index of the monitor to capture from (0-indexed)
            history_seconds: Duration to track in the rolling window
            backend: Capture backend name (auto/windows/macos)
            timeout_ms: Capture timeout in milliseconds
            dip_threshold_ms: Frame time threshold that counts as a dip
            idle_sleep_ms: Optional sleep after each frame to reduce CPU pressure
            crossing_correlation_ms: Time window to correlate dips with monitor crossings
        """
        self.monitor_index = monitor_index
        self.history_seconds = history_seconds
        self.backend = backend
        self.timeout_ms = timeout_ms
        self.dip_threshold_ms = dip_threshold_ms
        self.idle_sleep_s = max(0.0, idle_sleep_ms / 1000.0)
        self.crossing_correlation_s = max(0.0, crossing_correlation_ms / 1000.0)
        self.running = False
        
        # Performance tracking
        self.frame_times = deque(maxlen=history_seconds * 100)  # Store up to ~100 frames per second
        self.frame_time_ms = deque(maxlen=history_seconds * 100)
        self.frame_timestamps = deque(maxlen=history_seconds * 100)
        self.start_time = None
        self.dip_count = 0
        self.crossing_count = 0
        self.dips_near_crossing = 0
        self.last_crossing_time = None
        self.monitor_targets = []
        self.current_cursor_monitor = None
        
        # Lifetime min/max tracking
        self.lifetime_min_fps = float('inf')
        self.lifetime_max_fps = 0.0
        
        # GUI components
        self.root = tk.Tk()
        self.root.title("Capture Library Performance Monitor")
        self.root.geometry("1000x700")
        
        # Create GUI layout
        self._create_widgets()
        
        # Start capture thread
        self.capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
        self.running = True
        self.capture_thread.start()
        
        # Start update thread for GUI
        self._schedule_update()
    
    def _create_widgets(self):
        """Create the GUI widgets."""
        # Main frame
        main_frame = ttk.Frame(self.root)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # Stats frame
        stats_frame = ttk.LabelFrame(main_frame, text="Performance Metrics", padding=10)
        stats_frame.pack(fill=tk.X, pady=(0, 10))
        
        # Create grid for stats
        self.current_fps_label = ttk.Label(stats_frame, text="Current FPS: --", font=("Arial", 14, "bold"))
        self.current_fps_label.grid(row=0, column=0, padx=10, pady=5, sticky=tk.W)
        
        self.average_fps_label = ttk.Label(stats_frame, text="Average FPS (1m): --", font=("Arial", 12))
        self.average_fps_label.grid(row=0, column=1, padx=10, pady=5, sticky=tk.W)
        
        self.min_fps_label = ttk.Label(stats_frame, text="Min FPS: --", font=("Arial", 12))
        self.min_fps_label.grid(row=1, column=0, padx=10, pady=5, sticky=tk.W)
        
        self.max_fps_label = ttk.Label(stats_frame, text="Max FPS: --", font=("Arial", 12))
        self.max_fps_label.grid(row=1, column=1, padx=10, pady=5, sticky=tk.W)
        
        self.p1_low_label = ttk.Label(stats_frame, text="1% Low FPS: --", font=("Arial", 12))
        self.p1_low_label.grid(row=2, column=0, padx=10, pady=5, sticky=tk.W)
        
        self.frame_count_label = ttk.Label(stats_frame, text="Frames Captured: 0", font=("Arial", 12))
        self.frame_count_label.grid(row=2, column=1, padx=10, pady=5, sticky=tk.W)

        self.p99_ms_label = ttk.Label(stats_frame, text="P99 Frame Time: -- ms", font=("Arial", 12))
        self.p99_ms_label.grid(row=3, column=0, padx=10, pady=5, sticky=tk.W)

        self.dips_label = ttk.Label(stats_frame, text="Dips: 0", font=("Arial", 12))
        self.dips_label.grid(row=3, column=1, padx=10, pady=5, sticky=tk.W)

        self.crossings_label = ttk.Label(stats_frame, text="Crossings: 0", font=("Arial", 12))
        self.crossings_label.grid(row=4, column=0, padx=10, pady=5, sticky=tk.W)

        self.correlation_label = ttk.Label(stats_frame, text="Dips near crossings: 0", font=("Arial", 12))
        self.correlation_label.grid(row=4, column=1, padx=10, pady=5, sticky=tk.W)
        
        self.elapsed_label = ttk.Label(stats_frame, text="Elapsed Time: 0s", font=("Arial", 12))
        self.elapsed_label.grid(row=5, column=0, columnspan=2, padx=10, pady=5, sticky=tk.W)
        
        # Graph frame
        graph_frame = ttk.LabelFrame(main_frame, text="Framerate Over Time (Rolling 1-Minute Window)", padding=10)
        graph_frame.pack(fill=tk.BOTH, expand=True)
        
        # Create matplotlib figure
        self.fig = Figure(figsize=(9, 4), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_xlabel("Time (seconds ago)")
        self.ax.set_ylabel("FPS")
        self.ax.set_title("Average FPS (1-second rolling average)")
        self.ax.grid(True, alpha=0.3)
        
        # Embed matplotlib in tkinter
        self.canvas = FigureCanvasTkAgg(self.fig, master=graph_frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        # Status frame
        status_frame = ttk.Frame(main_frame)
        status_frame.pack(fill=tk.X, pady=(10, 0))
        
        self.status_label = ttk.Label(status_frame, text="Initializing...", foreground="blue")
        self.status_label.pack(side=tk.LEFT)
        
        self.stop_button = ttk.Button(status_frame, text="Stop", command=self.stop)
        self.stop_button.pack(side=tk.RIGHT)
    
    def _capture_loop(self):
        """Main capture loop running in a separate thread."""
        try:
            if capture is None:
                self.status_label.config(
                    text="Capture Error: Python module 'capture' is not available.",
                    foreground="red"
                )
                return

            # Reduce GC-induced pause jitter while benchmarking capture stability.
            gc.disable()

            with capture.CaptureSession(
                backend=self.backend,
                monitor=self.monitor_index,
                timeout_ms=self.timeout_ms,
            ) as session:
                try:
                    self.monitor_targets = capture.list_targets(self.backend)
                except Exception:
                    self.monitor_targets = []

                # Prime capture once so we can show dimensions in status text.
                first_frame = session.grab()
                height, width = first_frame.shape[:2]
                self.status_label.config(
                    text=(
                        f"Capturing from Monitor {self.monitor_index} ({self.backend}): "
                        f"{width}x{height}"
                    ),
                    foreground="green"
                )

                self.start_time = time.perf_counter()
                
                while self.running:
                    frame_start = time.perf_counter()
                    
                    # Capture the screen as a NumPy BGR frame.
                    _frame = session.grab()
                    
                    frame_end = time.perf_counter()

                    cursor_monitor = _get_cursor_monitor_name(self.monitor_targets)
                    if cursor_monitor and cursor_monitor != self.current_cursor_monitor:
                        if self.current_cursor_monitor is not None:
                            self.crossing_count += 1
                            self.last_crossing_time = frame_end
                        self.current_cursor_monitor = cursor_monitor

                    frame_time = frame_end - frame_start
                    
                    if frame_time > 0:
                        fps = 1.0 / frame_time
                        frame_ms = frame_time * 1000.0
                        self.frame_times.append(fps)
                        self.frame_time_ms.append(frame_ms)
                        self.frame_timestamps.append(frame_end)

                        if frame_ms > self.dip_threshold_ms:
                            self.dip_count += 1
                            if self.last_crossing_time is not None:
                                if (frame_end - self.last_crossing_time) <= self.crossing_correlation_s:
                                    self.dips_near_crossing += 1
                        
                        # Track lifetime min/max
                        self.lifetime_min_fps = min(self.lifetime_min_fps, fps)
                        self.lifetime_max_fps = max(self.lifetime_max_fps, fps)
                    
                    if self.idle_sleep_s > 0:
                        time.sleep(self.idle_sleep_s)
        
        except Exception as e:
            self.status_label.config(
                text=f"Capture Error: {str(e)}",
                foreground="red"
            )
            traceback.print_exc()
        finally:
            gc.enable()
    
    def _calculate_metrics(self):
        """Calculate performance metrics from captured data."""
        if not self.frame_times:
            return None
        
        fps_array = np.array(list(self.frame_times))
        frame_ms_array = np.array(list(self.frame_time_ms)) if self.frame_time_ms else np.array([])

        dip_rate_per_min = 0.0
        if self.start_time:
            elapsed_seconds = max(1e-6, time.perf_counter() - self.start_time)
            dip_rate_per_min = self.dip_count * 60.0 / elapsed_seconds
        
        metrics = {
            'current': fps_array[-1] if len(fps_array) > 0 else 0,
            'average': np.mean(fps_array),
            'min': self.lifetime_min_fps if self.lifetime_min_fps != float('inf') else 0,
            'max': self.lifetime_max_fps,
            'p1_low': np.percentile(fps_array, 1),  # 1st percentile is the 1% low
            'frame_count': len(self.frame_times),
            'p99_ms': np.percentile(frame_ms_array, 99) if len(frame_ms_array) > 0 else 0.0,
            'dip_count': self.dip_count,
            'dip_rate_per_min': dip_rate_per_min,
            'crossing_count': self.crossing_count,
            'dips_near_crossing': self.dips_near_crossing,
            'cursor_monitor': self.current_cursor_monitor,
        }
        
        return metrics
    
    def _get_rolling_averages(self):
        """Get 1-second rolling averages of FPS."""
        if len(self.frame_timestamps) < 2:
            return [], []
        
        fps_data = list(self.frame_times)
        timestamps = list(self.frame_timestamps)
        
        current_time = timestamps[-1]
        window_start = current_time - self.history_seconds
        
        # Create 1-second buckets
        rolling_averages = []
        time_labels = []
        
        for i in range(self.history_seconds, 0, -1):
            bucket_end = current_time - (i - 1)
            bucket_start = current_time - i
            
            # Find all FPS values in this bucket
            bucket_fps = [
                fps for fps, ts in zip(fps_data, timestamps)
                if bucket_start <= ts < bucket_end
            ]
            
            if bucket_fps:
                rolling_averages.append(np.mean(bucket_fps))
            else:
                rolling_averages.append(0)
            
            time_labels.append(i - 1)
        
        return time_labels, rolling_averages
    
    def _update_gui(self):
        """Update the GUI with current metrics."""
        metrics = self._calculate_metrics()
        
        if metrics:
            self.current_fps_label.config(
                text=f"Current FPS: {metrics['current']:.2f}"
            )
            self.average_fps_label.config(
                text=f"Average FPS (1m): {metrics['average']:.2f}"
            )
            self.min_fps_label.config(
                text=f"Min FPS: {metrics['min']:.2f}"
            )
            self.max_fps_label.config(
                text=f"Max FPS: {metrics['max']:.2f}"
            )
            self.p1_low_label.config(
                text=f"1% Low FPS: {metrics['p1_low']:.2f}"
            )
            self.frame_count_label.config(
                text=f"Frames Captured: {metrics['frame_count']}"
            )
            self.p99_ms_label.config(
                text=f"P99 Frame Time: {metrics['p99_ms']:.2f} ms"
            )
            self.dips_label.config(
                text=(
                    f"Dips (> {self.dip_threshold_ms:.1f} ms): {metrics['dip_count']} "
                    f"({metrics['dip_rate_per_min']:.1f}/min)"
                )
            )
            self.crossings_label.config(
                text=f"Crossings: {metrics['crossing_count']}"
            )
            self.correlation_label.config(
                text=(
                    f"Dips within {self.crossing_correlation_s * 1000:.0f}ms of crossing: "
                    f"{metrics['dips_near_crossing']}"
                )
            )
            
            # Update elapsed time
            if self.start_time:
                elapsed = int(time.perf_counter() - self.start_time)
                cursor_monitor = metrics.get('cursor_monitor') or "unknown"
                self.elapsed_label.config(
                    text=f"Elapsed Time: {elapsed}s | Cursor on: {cursor_monitor}"
                )
        
        # Update graph
        time_labels, rolling_averages = self._get_rolling_averages()
        
        if time_labels and rolling_averages:
            self.ax.clear()
            self.ax.plot(time_labels, rolling_averages, linewidth=2, color='steelblue')
            self.ax.set_xlabel("Time (seconds ago)")
            self.ax.set_ylabel("FPS")
            self.ax.set_title("Average FPS (1-second rolling average)")
            self.ax.grid(True, alpha=0.3)
            self.ax.invert_xaxis()  # Show current time on the right
            
            if rolling_averages:
                # Set Y-axis limits with some padding
                max_fps = max(rolling_averages) if rolling_averages else 100
                min_fps = min(rolling_averages) if rolling_averages else 0
                padding = (max_fps - min_fps) * 0.1 if (max_fps - min_fps) > 0 else 10
                self.ax.set_ylim(max(0, min_fps - padding), max_fps + padding)
            
            self.canvas.draw()
    
    def _schedule_update(self):
        """Schedule the next GUI update."""
        if self.running:
            self._update_gui()
            self.root.after(500, self._schedule_update)  # Update every 500ms
    
    def stop(self):
        """Stop the monitor and close the window."""
        self.running = False
        self.root.quit()
    
    def run(self):
        """Run the GUI event loop."""
        self.root.mainloop()


def _parse_args():
    parser = argparse.ArgumentParser(description="Capture Library performance monitor")
    parser.add_argument(
        "--monitor",
        type=int,
        default=1,
        help="Monitor number using --monitor-base (defaults to 1-based)",
    )
    parser.add_argument(
        "--monitor-base",
        type=int,
        choices=[0, 1],
        default=1,
        help="Interpret --monitor as 0-based or 1-based",
    )
    parser.add_argument(
        "--list-monitors",
        action="store_true",
        help="List available monitors with both 0-based and 1-based numbering, then exit",
    )
    parser.add_argument(
        "--history-seconds",
        type=int,
        default=60,
        help="Rolling metrics window length in seconds",
    )
    parser.add_argument(
        "--backend",
        choices=["auto", "windows", "macos"],
        default="auto",
        help="Capture backend",
    )
    parser.add_argument(
        "--timeout-ms",
        type=int,
        default=3000,
        help="Frame capture timeout in milliseconds",
    )
    parser.add_argument(
        "--dip-threshold-ms",
        type=float,
        default=16.7,
        help="Frame-time threshold used to count a dip",
    )
    parser.add_argument(
        "--idle-sleep-ms",
        type=float,
        default=0.0,
        help="Optional sleep per frame to reduce CPU pressure (default 0)",
    )
    parser.add_argument(
        "--crossing-correlation-ms",
        type=float,
        default=250.0,
        help="Time window for counting dips near monitor-boundary crossings",
    )
    return parser.parse_args()


def _resolve_monitor_index(monitor_value, monitor_base):
    if monitor_base == 1:
        return monitor_value - 1
    return monitor_value


def _get_windows_refresh_hz(display_name):
    if sys.platform != "win32":
        return None

    try:
        class DEVMODEW(ctypes.Structure):
            _fields_ = [
                ("dmDeviceName", ctypes.c_wchar * 32),
                ("dmSpecVersion", ctypes.c_ushort),
                ("dmDriverVersion", ctypes.c_ushort),
                ("dmSize", ctypes.c_ushort),
                ("dmDriverExtra", ctypes.c_ushort),
                ("dmFields", ctypes.c_ulong),
                ("dmPositionX", ctypes.c_long),
                ("dmPositionY", ctypes.c_long),
                ("dmDisplayOrientation", ctypes.c_ulong),
                ("dmDisplayFixedOutput", ctypes.c_ulong),
                ("dmColor", ctypes.c_short),
                ("dmDuplex", ctypes.c_short),
                ("dmYResolution", ctypes.c_short),
                ("dmTTOption", ctypes.c_short),
                ("dmCollate", ctypes.c_short),
                ("dmFormName", ctypes.c_wchar * 32),
                ("dmLogPixels", ctypes.c_ushort),
                ("dmBitsPerPel", ctypes.c_ulong),
                ("dmPelsWidth", ctypes.c_ulong),
                ("dmPelsHeight", ctypes.c_ulong),
                ("dmDisplayFlags", ctypes.c_ulong),
                ("dmDisplayFrequency", ctypes.c_ulong),
                ("dmICMMethod", ctypes.c_ulong),
                ("dmICMIntent", ctypes.c_ulong),
                ("dmMediaType", ctypes.c_ulong),
                ("dmDitherType", ctypes.c_ulong),
                ("dmReserved1", ctypes.c_ulong),
                ("dmReserved2", ctypes.c_ulong),
                ("dmPanningWidth", ctypes.c_ulong),
                ("dmPanningHeight", ctypes.c_ulong),
            ]

        ENUM_CURRENT_SETTINGS = -1
        devmode = DEVMODEW()
        devmode.dmSize = ctypes.sizeof(DEVMODEW)
        ok = ctypes.windll.user32.EnumDisplaySettingsW(
            ctypes.c_wchar_p(display_name),
            ENUM_CURRENT_SETTINGS,
            ctypes.byref(devmode),
        )
        if ok:
            hz = int(devmode.dmDisplayFrequency)
            return hz if hz > 0 else None
    except Exception:
        return None

    return None


def _get_cursor_position():
    if sys.platform != "win32":
        return None

    class POINT(ctypes.Structure):
        _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

    point = POINT()
    if ctypes.windll.user32.GetCursorPos(ctypes.byref(point)):
        return point.x, point.y
    return None


def _get_cursor_monitor_name(targets):
    pos = _get_cursor_position()
    if pos is None or not targets:
        return None

    x, y = pos
    for target in targets:
        bounds = target.get("bounds")
        if not bounds:
            continue
        bx, by, bw, bh = bounds
        if bx <= x < (bx + bw) and by <= y < (by + bh):
            return target.get("name", "<unnamed>")
    return None


def _list_monitors(backend):
    if capture is None:
        print("Capture Error: Python module 'capture' is not available.")
        return 1

    try:
        targets = capture.list_targets(backend)
    except Exception as exc:
        print(f"Capture Error: failed to list monitors: {exc}")
        return 1

    if not targets:
        print("No capture targets found.")
        return 1

    print("Available monitors:")
    for idx0, target in enumerate(targets):
        idx1 = idx0 + 1
        name = target.get("name", "<unnamed>")
        refresh_hz = _get_windows_refresh_hz(name)
        bounds = target.get("bounds")
        if bounds is None:
            bounds_text = "bounds: <unknown>"
        else:
            x, y, w, h = bounds
            bounds_text = f"bounds: x={x}, y={y}, w={w}, h={h}"
        hz_text = f"refresh: {refresh_hz}Hz" if refresh_hz is not None else "refresh: unknown"
        print(f"  1-based {idx1:>2} | 0-based {idx0:>2} | {name} | {hz_text} | {bounds_text}")

    return 0


if __name__ == "__main__":
    args = _parse_args()

    if args.list_monitors:
        raise SystemExit(_list_monitors(args.backend))

    resolved_monitor_index = _resolve_monitor_index(args.monitor, args.monitor_base)
    if resolved_monitor_index < 0:
        raise SystemExit("Error: resolved monitor index is negative. Check --monitor and --monitor-base.")

    monitor = PerformanceMonitor(
        monitor_index=resolved_monitor_index,
        history_seconds=args.history_seconds,
        backend=args.backend,
        timeout_ms=args.timeout_ms,
        dip_threshold_ms=args.dip_threshold_ms,
        idle_sleep_ms=args.idle_sleep_ms,
        crossing_correlation_ms=args.crossing_correlation_ms,
    )
    monitor.run()
