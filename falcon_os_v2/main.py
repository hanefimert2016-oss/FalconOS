#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FalconOS v2.0 Alpha - Main System Bootstrap
Integrates kernel, filesystem, and GUI into a complete operating system simulation
"""

import sys
import os
import time
import json
import threading
from datetime import datetime
from typing import Dict, Any, Optional

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.kernel import get_kernel, Kernel
from fs.filesystem import get_filesystem, VirtualFileSystem

# GUI imports are optional - only import when needed
try:
    from gui.desktop import launch_gui, DesktopEnvironment, Theme
    GUI_AVAILABLE = True
except ImportError:
    GUI_AVAILABLE = False
    launch_gui = None
    DesktopEnvironment = None
    Theme = None

class FalconOS:
    """Main FalconOS Operating System Class"""
    
    VERSION = "2.0.0-alpha"
    BUILD_NUMBER = 20241201
    CODENAME = "Sky Hunter"
    
    def __init__(self):
        self.kernel: Optional[Kernel] = None
        self.filesystem: Optional[VirtualFileSystem] = None
        self.desktop: Optional[DesktopEnvironment] = None
        self.boot_log: list = []
        self.start_time: Optional[float] = None
        self.services: Dict[str, bool] = {}
        
    def log_boot(self, message: str):
        """Log boot message"""
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_entry = f"[{timestamp}] {message}"
        self.boot_log.append(log_entry)
        print(log_entry)
        
    def boot(self, gui: bool = True):
        """Boot the operating system"""
        self.start_time = time.time()
        
        print("\n" + "="*60)
        print("  🦅 FALCONOS v{} - '{}'".format(self.VERSION, self.CODENAME))
        print("  Build: {}".format(self.BUILD_NUMBER))
        print("="*60 + "\n")
        
        self.log_boot("Starting FalconOS bootstrap...")
        
        # Stage 1: Initialize kernel
        self.log_boot("[Stage 1/5] Initializing kernel subsystems...")
        try:
            self.kernel = get_kernel()
            self.log_boot("✓ Kernel module loaded")
            self.log_boot(f"  Version: {self.kernel.VERSION}")
        except Exception as e:
            self.log_boot(f"✗ Kernel initialization failed: {e}")
            return False
        
        # Stage 2: Initialize file system
        self.log_boot("[Stage 2/5] Initializing virtual file system...")
        try:
            self.filesystem = get_filesystem()
            self.filesystem.format("FALCON_ROOT")
            self.kernel.initialize(self.filesystem)
            self.log_boot("✓ File system mounted")
            self.log_boot(f"  Volume: FALCON_ROOT")
            self.log_boot(f"  Total inodes: {len(self.filesystem.inodes)}")
        except Exception as e:
            self.log_boot(f"✗ File system initialization failed: {e}")
            return False
        
        # Stage 3: Create default directory structure
        self.log_boot("[Stage 3/5] Creating default directory structure...")
        try:
            self._create_default_structure()
            self.log_boot("✓ Directory structure created")
        except Exception as e:
            self.log_boot(f"✗ Directory creation failed: {e}")
            return False
        
        # Stage 4: Start system services
        self.log_boot("[Stage 4/5] Starting system services...")
        try:
            self._start_services()
            self.log_boot("✓ System services started")
        except Exception as e:
            self.log_boot(f"✗ Service startup failed: {e}")
            return False
        
        # Stage 5: Launch GUI (optional)
        if gui:
            self.log_boot("[Stage 5/5] Launching graphical desktop environment...")
            try:
                self.log_boot("✓ Starting GUI subsystem")
                self.log_boot(f"  Theme: Falcon (Dark)")
                self.log_boot(f"  Resolution: Auto-detect")
            except Exception as e:
                self.log_boot(f"✗ GUI initialization failed: {e}")
                return False
        else:
            self.log_boot("[Stage 5/5] Skipping GUI (headless mode)")
        
        # Boot complete
        boot_time = time.time() - self.start_time
        self.log_boot("="*60)
        self.log_boot(f"🎉 FalconOS booted successfully in {boot_time:.2f} seconds!")
        self.log_boot("="*60)
        
        return True
    
    def _create_default_structure(self):
        """Create default OS directory structure"""
        # Create user directories
        dirs_to_create = [
            "/home/user",
            "/home/user/Documents",
            "/home/user/Downloads",
            "/home/user/Pictures",
            "/home/user/Music",
            "/home/user/Videos",
            "/home/user/Desktop",
            "/var/log",
            "/var/tmp",
            "/opt/apps",
            "/etc/config"
        ]
        
        for dir_path in dirs_to_create:
            if not self.filesystem.exists(dir_path):
                self.filesystem.mkdir(dir_path)
        
        # Create system files
        system_files = {
            "/etc/os-release": b"FALCON_NAME=FalconOS\nVERSION=2.0.0-alpha\nID=falconos\nPRETTY_NAME=FalconOS v2.0 Sky Hunter",
            "/etc/hostname": b"falconos",
            "/home/user/welcome.txt": b"Welcome to FalconOS v2.0!\n\nThis is your new operating system.\nEnjoy exploring!",
            "/home/user/README.md": b"# FalconOS v2.0\n\n## Features\n- Advanced virtual file system\n- Modern GUI desktop\n- Multi-tasking kernel\n- Performance optimized\n\n## Getting Started\nOpen the terminal and start exploring!"
        }
        
        for file_path, content in system_files.items():
            if not self.filesystem.exists(file_path):
                self.filesystem.create_file(file_path, content=content)
        
        # Create sample applications
        apps = {
            "/bin/hello": b"#!/bin/falcon\necho 'Hello from FalconOS!'",
            "/bin/systeminfo": b"#!/bin/falcon\nshow system information",
            "/bin/filemanager": b"#!/bin/falcon\nlaunch file manager"
        }
        
        for app_path, content in apps.items():
            if not self.filesystem.exists(app_path):
                self.filesystem.create_file(app_path, permissions=0o755, content=content)
    
    def _start_services(self):
        """Start core system services"""
        service_list = [
            ("process_scheduler", True),
            ("memory_manager", True),
            ("file_system_cache", True),
            ("device_manager", True),
            ("network_stack", False),  # Not implemented yet
            ("audio_server", False),   # Not implemented yet
            ("print_spooler", False),  # Not implemented yet
            ("update_service", True),
            ("security_monitor", True),
            ("log_daemon", True)
        ]
        
        for service_name, enabled in service_list:
            self.services[service_name] = enabled
            status = "✓" if enabled else "○"
            self.log_boot(f"  {status} {service_name}")
    
    def get_system_info(self) -> Dict[str, Any]:
        """Get comprehensive system information"""
        info = {
            "os": {
                "name": "FalconOS",
                "version": self.VERSION,
                "build": self.BUILD_NUMBER,
                "codename": self.CODENAME,
                "uptime_seconds": time.time() - self.start_time if self.start_time else 0
            },
            "kernel": self.kernel.get_system_info() if self.kernel else None,
            "filesystem": self.filesystem.get_stats() if self.filesystem else None,
            "services": self.services,
            "boot_log": self.boot_log[-20:]  # Last 20 boot messages
        }
        return info
    
    def run_cli(self):
        """Run command-line interface"""
        print("\nFalconOS CLI v2.0 - Type 'help' for commands\n")
        
        while True:
            try:
                cmd = input("user@falconos:~$ ").strip()
                
                if cmd == "exit" or cmd == "quit":
                    break
                elif cmd == "help":
                    self._cli_help()
                elif cmd == "ls":
                    self._cli_ls("/")
                elif cmd == "sysinfo":
                    self._cli_sysinfo()
                elif cmd == "ps":
                    self._cli_ps()
                elif cmd.startswith("cat "):
                    self._cli_cat(cmd[4:])
                elif cmd.startswith("cd "):
                    print("cd not fully implemented in alpha")
                elif cmd == "clear":
                    os.system('clear' if os.name != 'nt' else 'cls')
                elif cmd == "reboot":
                    print("Rebooting...")
                    break
                elif cmd == "shutdown":
                    self.shutdown()
                    break
                else:
                    print(f"Command not found: {cmd}")
                    
            except KeyboardInterrupt:
                print("\nUse 'exit' to quit")
            except EOFError:
                break
    
    def _cli_help(self):
        """Show CLI help"""
        help_text = """
Available Commands:
  help       - Show this help message
  ls         - List directory contents
  cat <file> - Display file contents
  sysinfo    - Show system information
  ps         - List running processes
  clear      - Clear screen
  reboot     - Reboot system
  shutdown   - Shutdown system
  exit       - Exit CLI
"""
        print(help_text)
    
    def _cli_ls(self, path: str = "/"):
        """List directory"""
        if self.filesystem and self.filesystem.exists(path):
            entries = self.filesystem.list_directory(path)
            for entry in entries:
                icon = "📁" if entry['type'] == 'directory' else "📄"
                print(f"{icon} {entry['name']}")
        else:
            print(f"Path not found: {path}")
    
    def _cli_cat(self, path: str):
        """Display file contents"""
        if self.filesystem and self.filesystem.exists(path):
            try:
                content = self.filesystem.read_file(path)
                print(content.decode('utf-8'))
            except Exception as e:
                print(f"Error reading file: {e}")
        else:
            print(f"File not found: {path}")
    
    def _cli_sysinfo(self):
        """Show system info"""
        info = self.get_system_info()
        print(json.dumps(info['os'], indent=2))
        if info['filesystem']:
            print("\nFile System:")
            print(f"  Total Files: {info['filesystem']['total_files']}")
            print(f"  Total Directories: {info['filesystem']['total_directories']}")
            print(f"  Storage Used: {info['filesystem']['device']['used_size_mb']:.1f} MB")
    
    def _cli_ps(self):
        """List processes"""
        if self.kernel:
            processes = self.kernel.scheduler.get_process_list()
            print(f"{'PID':<8} {'Name':<20} {'State':<10} {'Priority':<10}")
            print("-" * 50)
            for proc in processes:
                print(f"{proc['pid']:<8} {proc['name']:<20} {proc['state']:<10} {proc['priority']:<10}")
    
    def shutdown(self):
        """Shutdown the system"""
        print("\nShutting down FalconOS...")
        
        if self.kernel:
            self.kernel.shutdown()
        
        print("System halted.")
        print("It's now safe to turn off your computer.")
    
    def launch_gui(self):
        """Launch the graphical interface"""
        if not GUI_AVAILABLE:
            print("Error: GUI not available. Install tkinter to use GUI mode.")
            return False
        if self.boot(gui=True):
            launch_gui()
            return True
        return False

def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description='FalconOS v2.0 Operating System')
    parser.add_argument('--gui', action='store_true', help='Launch with GUI')
    parser.add_argument('--cli', action='store_true', help='Launch in CLI mode')
    parser.add_argument('--info', action='store_true', help='Show system info and exit')
    parser.add_argument('--headless', action='store_true', help='Run in headless mode (no GUI)')
    
    args = parser.parse_args()
    
    os_system = FalconOS()
    
    if args.info:
        # Just show info without full boot
        print(f"FalconOS v{os_system.VERSION} '{os_system.CODENAME}'")
        print(f"Build: {os_system.BUILD_NUMBER}")
        return
    
    # Default to CLI if no args, GUI if --gui, headless if --headless
    if args.headless:
        os_system.boot(gui=False)
        os_system.run_cli()
    elif args.gui:
        os_system.boot(gui=True)
        os_system.launch_gui()
    elif args.cli:
        os_system.boot(gui=False)
        os_system.run_cli()
    else:
        # Default: boot and run CLI
        os_system.boot(gui=False)
        os_system.run_cli()

if __name__ == "__main__":
    main()
