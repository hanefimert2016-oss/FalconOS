#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FalconOS v2.0 Alpha - Advanced Graphical User Interface
Modern desktop environment with window management, widgets, and hardware acceleration simulation
"""

import tkinter as tk
from tkinter import ttk, font as tkfont
import threading
import time
import json
import math
from datetime import datetime
from typing import Dict, List, Optional, Any, Callable, Tuple
from dataclasses import dataclass, field
from enum import Enum
import os

class WindowState(Enum):
    NORMAL = "normal"
    MINIMIZED = "minimized"
    MAXIMIZED = "maximized"
    FULLSCREEN = "fullscreen"

class Theme(Enum):
    LIGHT = "light"
    DARK = "dark"
    FALCON = "falcon"  # Custom FalconOS theme

@dataclass
class Color:
    """Color representation with RGB values"""
    r: int
    g: int
    b: int
    
    def to_hex(self) -> str:
        return f"#{self.r:02x}{self.g:02x}{self.b:02x}"
    
    @classmethod
    def from_hex(cls, hex_color: str) -> 'Color':
        hex_color = hex_color.lstrip('#')
        return cls(
            r=int(hex_color[0:2], 16),
            g=int(hex_color[2:4], 16),
            b=int(hex_color[4:6], 16)
        )

# FalconOS Color Palette
THEMES = {
    Theme.LIGHT: {
        "background": Color(245, 245, 245),
        "foreground": Color(30, 30, 30),
        "primary": Color(66, 133, 244),
        "secondary": Color(52, 168, 83),
        "accent": Color(251, 140, 0),
        "error": Color(234, 67, 53),
        "window_bg": Color(255, 255, 255),
        "panel_bg": Color(240, 240, 240),
        "border": Color(200, 200, 200)
    },
    Theme.DARK: {
        "background": Color(30, 30, 30),
        "foreground": Color(240, 240, 240),
        "primary": Color(88, 166, 255),
        "secondary": Color(80, 200, 120),
        "accent": Color(255, 170, 0),
        "error": Color(255, 82, 82),
        "window_bg": Color(40, 40, 40),
        "panel_bg": Color(50, 50, 50),
        "border": Color(70, 70, 70)
    },
    Theme.FALCON: {
        "background": Color(26, 26, 46),
        "foreground": Color(233, 237, 255),
        "primary": Color(99, 110, 173),
        "secondary": Color(156, 163, 175),
        "accent": Color(255, 121, 198),
        "error": Color(255, 85, 85),
        "window_bg": Color(35, 35, 54),
        "panel_bg": Color(41, 41, 64),
        "border": Color(62, 62, 92)
    }
}

@dataclass
class Window:
    """Window representation"""
    id: int
    title: str
    x: int
    y: int
    width: int
    height: int
    state: WindowState = WindowState.NORMAL
    resizable: bool = True
    minimizable: bool = True
    maximizable: bool = True
    content_widget: Optional[tk.Widget] = None
    on_close: Optional[Callable] = None
    focused: bool = False

class WindowManager:
    """Advanced window manager with compositing simulation"""
    
    def __init__(self, root: tk.Tk):
        self.root = root
        self.windows: Dict[int, Window] = {}
        self.next_window_id = 1
        self.active_window: Optional[int] = None
        self.desktop_area = None
        self.taskbar = None
        self._lock = threading.Lock()
        
    def create_window(self, title: str, width: int = 800, height: int = 600,
                     content: Optional[Callable] = None, 
                     on_close: Optional[Callable] = None) -> int:
        with self._lock:
            window_id = self.next_window_id
            self.next_window_id += 1
            
            # Center window on screen
            screen_width = self.root.winfo_screenwidth()
            screen_height = self.root.winfo_screenheight()
            x = (screen_width - width) // 2
            y = (screen_height - height) // 2
            
            window = Window(
                id=window_id,
                title=title,
                x=x,
                y=y,
                width=width,
                height=height,
                on_close=on_close
            )
            
            self.windows[window_id] = window
            
            if content:
                window.content_widget = content(self.root, window)
            
            return window_id
    
    def focus_window(self, window_id: int):
        with self._lock:
            if window_id in self.windows:
                for wid in self.windows:
                    self.windows[wid].focused = (wid == window_id)
                self.active_window = window_id
    
    def close_window(self, window_id: int):
        with self._lock:
            if window_id in self.windows:
                window = self.windows[window_id]
                if window.on_close:
                    window.on_close()
                del self.windows[window_id]
                if self.active_window == window_id:
                    self.active_window = None
    
    def get_window_list(self) -> List[Dict[str, Any]]:
        return [
            {
                "id": w.id,
                "title": w.title,
                "state": w.state.value,
                "focused": w.focused
            }
            for w in self.windows.values()
        ]

class DesktopEnvironment:
    """Main desktop environment with panel, widgets, and system tray"""
    
    def __init__(self, root: tk.Tk, theme: Theme = Theme.FALCON):
        self.root = root
        self.theme = theme
        self.colors = THEMES[theme]
        self.window_manager = WindowManager(root)
        self.widgets: List[tk.Widget] = []
        self.system_tray_items: Dict[str, Dict] = {}
        self.notifications: List[Dict] = []
        self.running = False
        
        self._setup_desktop()
        self._setup_taskbar()
        self._setup_system_tray()
        
    def _setup_desktop(self):
        """Setup desktop area"""
        self.desktop_area = tk.Frame(
            self.root,
            bg=self.colors["background"].to_hex()
        )
        self.desktop_area.pack(fill=tk.BOTH, expand=True)
        
        # Desktop background gradient (simulated)
        self._draw_background()
        
    def _draw_background(self):
        """Draw animated background"""
        canvas = tk.Canvas(
            self.desktop_area,
            bg=self.colors["background"].to_hex(),
            highlightthickness=0
        )
        canvas.pack(fill=tk.BOTH, expand=True)
        
        # Draw FalconOS logo pattern
        width = self.root.winfo_screenwidth()
        height = self.root.winfo_screenheight()
        
        # Gradient effect
        for i in range(0, height, 10):
            color_val = int(26 + (i / height) * 10)
            canvas.create_rectangle(
                0, i, width, i + 10,
                fill=f"#{color_val:02x}{color_val:02x}{min(color_val + 20, 46):02x}",
                outline=""
            )
        
        # Falcon logo (stylized)
        center_x, center_y = width // 2, height // 2
        canvas.create_oval(
            center_x - 100, center_y - 100,
            center_x + 100, center_y + 100,
            outline=self.colors["primary"].to_hex(),
            width=3
        )
        canvas.create_text(
            center_x, center_y,
            text="🦅",
            font=("Segoe UI", 80),
            fill=self.colors["accent"].to_hex()
        )
        canvas.create_text(
            center_x, center_y + 140,
            text="FalconOS v2.0",
            font=("Segoe UI", 24, "bold"),
            fill=self.colors["foreground"].to_hex()
        )
        
    def _setup_taskbar(self):
        """Setup bottom taskbar"""
        self.taskbar = tk.Frame(
            self.root,
            bg=self.colors["panel_bg"].to_hex(),
            height=48
        )
        self.taskbar.pack(side=tk.BOTTOM, fill=tk.X)
        self.taskbar.pack_propagate(False)
        
        # Start button
        start_btn = tk.Button(
            self.taskbar,
            text="🦅 Start",
            bg=self.colors["primary"].to_hex(),
            fg="white",
            bd=0,
            padx=15,
            pady=8,
            command=self._open_start_menu
        )
        start_btn.pack(side=tk.LEFT, padx=5, pady=5)
        
        # Task buttons container
        self.task_buttons_frame = tk.Frame(
            self.taskbar,
            bg=self.colors["panel_bg"].to_hex()
        )
        self.task_buttons_frame.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=10)
        
        # System tray container
        self.tray_frame = tk.Frame(
            self.taskbar,
            bg=self.colors["panel_bg"].to_hex()
        )
        self.tray_frame.pack(side=tk.RIGHT, padx=10)
        
        # Clock
        self.clock_label = tk.Label(
            self.tray_frame,
            text="",
            bg=self.colors["panel_bg"].to_hex(),
            fg=self.colors["foreground"].to_hex(),
            font=("Segoe UI", 10)
        )
        self.clock_label.pack(side=tk.RIGHT, padx=10)
        self._update_clock()
        
    def _setup_system_tray(self):
        """Setup system tray icons"""
        self.add_tray_item("network", "🌐", "Network Connected")
        self.add_tray_item("volume", "🔊", "Volume: 75%")
        self.add_tray_item("battery", "🔋", "Battery: 100%")
        
    def add_tray_item(self, name: str, icon: str, tooltip: str):
        """Add item to system tray"""
        label = tk.Label(
            self.tray_frame,
            text=icon,
            bg=self.colors["panel_bg"].to_hex(),
            fg=self.colors["foreground"].to_hex(),
            font=("Segoe UI", 12),
            cursor="hand2"
        )
        label.pack(side=tk.RIGHT, padx=5)
        
        self.system_tray_items[name] = {
            "widget": label,
            "icon": icon,
            "tooltip": tooltip
        }
        
    def _update_clock(self):
        """Update clock display"""
        current_time = datetime.now().strftime("%H:%M:%S\n%d/%m/%Y")
        self.clock_label.config(text=current_time)
        self.root.after(1000, self._update_clock)
        
    def _open_start_menu(self):
        """Open start menu"""
        menu = tk.Toplevel(self.root)
        menu.title("Start Menu")
        menu.geometry("400x600")
        menu.configure(bg=self.colors["window_bg"].to_hex())
        
        # Make it stay on top
        menu.attributes('-topmost', True)
        
        # Search box
        search_frame = tk.Frame(menu, bg=self.colors["primary"].to_hex())
        search_frame.pack(fill=tk.X, padx=10, pady=10)
        
        search_entry = tk.Entry(
            search_frame,
            font=("Segoe UI", 12),
            bd=0,
            relief=tk.FLAT
        )
        search_entry.pack(fill=tk.X, ipady=5)
        search_entry.insert(0, "Type to search...")
        
        # Apps grid
        apps_frame = tk.Frame(menu, bg=self.colors["window_bg"].to_hex())
        apps_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        apps = [
            ("📁 File Manager", self._open_file_manager),
            ("⚙️ Settings", self._open_settings),
            ("🌐 Web Browser", self._open_browser),
            ("📝 Text Editor", self._open_text_editor),
            ("💻 Terminal", self._open_terminal),
            ("🎮 Games", None),
            ("📊 System Monitor", self._open_system_monitor),
        ]
        
        for i, (app_name, callback) in enumerate(apps):
            btn = tk.Button(
                apps_frame,
                text=app_name,
                bg=self.colors["panel_bg"].to_hex(),
                fg=self.colors["foreground"].to_hex(),
                font=("Segoe UI", 11),
                bd=0,
                pady=10,
                anchor="w",
                padx=15,
                command=callback if callback else lambda: None
            )
            btn.grid(row=i//2, column=i%2, sticky="ew", padx=5, pady=5, fill=tk.BOTH)
        
        apps_frame.grid_columnconfigure(0, weight=1)
        apps_frame.grid_columnconfigure(1, weight=1)
        
        # Power buttons
        power_frame = tk.Frame(menu, bg=self.colors["panel_bg"].to_hex())
        power_frame.pack(fill=tk.X, padx=10, pady=10)
        
        tk.Button(
            power_frame,
            text="🔒 Lock",
            bg=self.colors["secondary"].to_hex(),
            fg="white",
            bd=0,
            padx=15,
            pady=5
        ).pack(side=tk.LEFT, padx=5)
        
        tk.Button(
            power_frame,
            text="🔄 Restart",
            bg=self.colors["accent"].to_hex(),
            fg="white",
            bd=0,
            padx=15,
            pady=5
        ).pack(side=tk.LEFT, padx=5)
        
        tk.Button(
            power_frame,
            text="⏻ Shutdown",
            bg=self.colors["error"].to_hex(),
            fg="white",
            bd=0,
            padx=15,
            pady=5,
            command=self.shutdown
        ).pack(side=tk.LEFT, padx=5)
        
    def _open_file_manager(self):
        """Open file manager window"""
        def create_content(root, window):
            frame = tk.Frame(root, bg=self.colors["window_bg"].to_hex())
            
            # Sidebar
            sidebar = tk.Frame(frame, bg=self.colors["panel_bg"].to_hex(), width=200)
            sidebar.pack(side=tk.LEFT, fill=tk.Y)
            
            places = ["🏠 Home", "📄 Documents", "🖼️ Pictures", "🎵 Music", "💾 Downloads"]
            for place in places:
                lbl = tk.Label(
                    sidebar,
                    text=place,
                    bg=self.colors["panel_bg"].to_hex(),
                    fg=self.colors["foreground"].to_hex(),
                    pady=10,
                    padx=10,
                    anchor="w"
                )
                lbl.pack(fill=tk.X)
            
            # Main area
            main = tk.Frame(frame, bg=self.colors["window_bg"].to_hex())
            main.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            
            # File grid
            files = ["📁 Projects", "📁 Work", "📄 README.md", "📝 notes.txt", "🖼️ photo.jpg"]
            for i, file in enumerate(files):
                lbl = tk.Label(
                    main,
                    text=file,
                    bg=self.colors["window_bg"].to_hex(),
                    fg=self.colors["foreground"].to_hex(),
                    pady=15,
                    padx=10,
                    anchor="w"
                )
                lbl.grid(row=i//3, column=i%3, sticky="ew", padx=5, pady=5)
            
            return frame
        
        self.window_manager.create_window("File Manager", 900, 600, content=create_content)
        
    def _open_settings(self):
        """Open settings window"""
        def create_content(root, window):
            frame = tk.Frame(root, bg=self.colors["window_bg"].to_hex())
            
            notebook = ttk.Notebook(frame)
            notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
            
            # Appearance tab
            appearance = tk.Frame(notebook, bg=self.colors["window_bg"].to_hex())
            notebook.add(appearance, text="Appearance")
            
            tk.Label(
                appearance,
                text="Theme:",
                bg=self.colors["window_bg"].to_hex(),
                fg=self.colors["foreground"].to_hex()
            ).pack(pady=10)
            
            theme_var = tk.StringVar(value=self.theme.name)
            for theme_name in Theme.__members__.keys():
                rb = tk.Radiobutton(
                    appearance,
                    text=theme_name,
                    variable=theme_var,
                    value=theme_name,
                    bg=self.colors["window_bg"].to_hex(),
                    fg=self.colors["foreground"].to_hex()
                )
                rb.pack(anchor="w", padx=20)
            
            # System tab
            system = tk.Frame(notebook, bg=self.colors["window_bg"].to_hex())
            notebook.add(system, text="System")
            
            tk.Label(
                system,
                text="FalconOS v2.0 Alpha",
                bg=self.colors["window_bg"].to_hex(),
                fg=self.colors["foreground"].to_hex(),
                font=("Segoe UI", 14, "bold")
            ).pack(pady=20)
            
            return frame
        
        self.window_manager.create_window("Settings", 600, 500, content=create_content)
        
    def _open_terminal(self):
        """Open terminal window"""
        def create_content(root, window):
            frame = tk.Frame(root, bg="#1e1e1e")
            
            text = tk.Text(
                frame,
                bg="#1e1e1e",
                fg="#00ff00",
                insertbackground="#00ff00",
                font=("Consolas", 11),
                bd=0
            )
            text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
            
            # Add welcome message
            text.insert(tk.END, "Welcome to FalconOS Terminal v2.0\n")
            text.insert(tk.END, "Type 'help' for available commands.\n\n")
            text.insert(tk.END, "user@falconos:~$ ")
            
            return frame
        
        self.window_manager.create_window("Terminal", 800, 500, content=create_content)
        
    def _open_browser(self):
        """Open web browser window"""
        def create_content(root, window):
            frame = tk.Frame(root, bg=self.colors["window_bg"].to_hex())
            
            # Address bar
            addr_frame = tk.Frame(frame, bg=self.colors["panel_bg"].to_hex())
            addr_frame.pack(fill=tk.X, padx=5, pady=5)
            
            tk.Button(addr_frame, text="◀", bd=0, padx=10).pack(side=tk.LEFT)
            tk.Button(addr_frame, text="▶", bd=0, padx=10).pack(side=tk.LEFT)
            tk.Button(addr_frame, text="⟳", bd=0, padx=10).pack(side=tk.LEFT)
            
            addr_entry = tk.Entry(
                addr_frame,
                font=("Segoe UI", 11),
                bd=1,
                relief=tk.SOLID
            )
            addr_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=10, ipady=3)
            addr_entry.insert(0, "https://falconos.dev")
            
            # Content area
            content = tk.Frame(frame, bg="white")
            content.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
            
            tk.Label(
                content,
                text="🦅 FalconOS Browser\n\nWelcome to the web!",
                bg="white",
                fg="#333",
                font=("Segoe UI", 24)
            ).pack(expand=True)
            
            return frame
        
        self.window_manager.create_window("Web Browser", 1000, 700, content=create_content)
        
    def _open_text_editor(self):
        """Open text editor window"""
        def create_content(root, window):
            frame = tk.Frame(root, bg=self.colors["window_bg"].to_hex())
            
            text = tk.Text(
                frame,
                bg=self.colors["window_bg"].to_hex(),
                fg=self.colors["foreground"].to_hex(),
                insertbackground=self.colors["foreground"].to_hex(),
                font=("Consolas", 11),
                bd=0
            )
            text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
            
            text.insert(tk.END, "# Welcome to FalconOS Text Editor\n\n")
            
            return frame
        
        self.window_manager.create_window("Text Editor", 800, 600, content=create_content)
        
    def _open_system_monitor(self):
        """Open system monitor window"""
        def create_content(root, window):
            frame = tk.Frame(root, bg=self.colors["window_bg"].to_hex())
            
            # CPU usage
            cpu_frame = tk.LabelFrame(
                frame,
                text="CPU Usage",
                bg=self.colors["window_bg"].to_hex(),
                fg=self.colors["foreground"].to_hex()
            )
            cpu_frame.pack(fill=tk.X, padx=10, pady=10)
            
            self.cpu_bar = ttk.Progressbar(
                cpu_frame,
                length=400,
                mode='determinate'
            )
            self.cpu_bar.pack(padx=10, pady=10)
            self.cpu_bar['value'] = 25
            
            # Memory usage
            mem_frame = tk.LabelFrame(
                frame,
                text="Memory Usage",
                bg=self.colors["window_bg"].to_hex(),
                fg=self.colors["foreground"].to_hex()
            )
            mem_frame.pack(fill=tk.X, padx=10, pady=10)
            
            self.mem_bar = ttk.Progressbar(
                mem_frame,
                length=400,
                mode='determinate'
            )
            self.mem_bar.pack(padx=10, pady=10)
            self.mem_bar['value'] = 40
            
            # Process list
            proc_frame = tk.LabelFrame(
                frame,
                text="Processes",
                bg=self.colors["window_bg"].to_hex(),
                fg=self.colors["foreground"].to_hex()
            )
            proc_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
            
            columns = ("PID", "Name", "CPU", "Memory")
            tree = ttk.Treeview(proc_frame, columns=columns, show="headings")
            for col in columns:
                tree.heading(col, text=col)
            tree.pack(fill=tk.BOTH, expand=True)
            
            # Sample processes
            processes = [
                (1, "kernel", "0.1%", "50MB"),
                (2, "init", "0.0%", "10MB"),
                (3, "desktop", "2.5%", "200MB"),
                (4, "browser", "15.2%", "512MB"),
            ]
            for pid, name, cpu, mem in processes:
                tree.insert("", tk.END, values=(pid, name, cpu, mem))
            
            # Update loop
            def update_stats():
                import random
                self.cpu_bar['value'] = random.uniform(10, 50)
                self.mem_bar['value'] = random.uniform(30, 60)
                root.after(2000, update_stats)
            
            update_stats()
            
            return frame
        
        self.window_manager.create_window("System Monitor", 700, 500, content=create_content)
        
    def show_notification(self, title: str, message: str, icon: str = "ℹ️"):
        """Show desktop notification"""
        notification = {
            "title": title,
            "message": message,
            "icon": icon,
            "time": datetime.now()
        }
        self.notifications.append(notification)
        
        # Create notification popup
        popup = tk.Toplevel(self.root)
        popup.overrideredirect(True)
        
        screen_width = self.root.winfo_screenwidth()
        x = screen_width - 320
        y = 100
        
        popup.geometry(f"300x100+{x}+{y}")
        popup.configure(bg=self.colors["primary"].to_hex())
        
        tk.Label(
            popup,
            text=f"{icon} {title}",
            bg=self.colors["primary"].to_hex(),
            fg="white",
            font=("Segoe UI", 11, "bold")
        ).pack(pady=5)
        
        tk.Label(
            popup,
            text=message,
            bg=self.colors["primary"].to_hex(),
            fg="white",
            font=("Segoe UI", 9)
        ).pack()
        
        # Auto-close after 5 seconds
        popup.after(5000, popup.destroy)
        
    def start(self):
        """Start the desktop environment"""
        self.running = True
        self.show_notification(
            "Welcome to FalconOS",
            "Desktop environment loaded successfully!",
            "🦅"
        )
        
    def shutdown(self):
        """Shutdown the desktop environment"""
        self.running = False
        self.root.quit()
        self.root.destroy()

def launch_gui():
    """Launch the FalconOS GUI"""
    root = tk.Tk()
    root.title("FalconOS v2.0 Desktop")
    
    # Fullscreen
    root.attributes('-fullscreen', True)
    
    # Create desktop environment
    desktop = DesktopEnvironment(root, theme=Theme.FALCON)
    desktop.start()
    
    # Bind escape key to exit fullscreen (for development)
    root.bind('<Escape>', lambda e: root.attributes('-fullscreen', False))
    
    root.mainloop()

if __name__ == "__main__":
    launch_gui()
