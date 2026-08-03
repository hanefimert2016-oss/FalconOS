# 🦅 FalconOS v2.0 Alpha

**"Sky Hunter"** - Build 20241201

A modern, high-performance operating system simulation built in Python with advanced features including a virtual file system, multi-tasking kernel, and beautiful graphical desktop environment.

## ✨ Features

### Core System
- **Microkernel Architecture** - Preemptive multitasking with priority-based scheduling
- **Virtual Memory Manager** - 512MB simulated RAM with 4KB page sizing
- **Journaling File System** - Advanced VFS with caching, permissions, and mount points
- **System Call Interface** - 11 syscalls for process and memory management
- **Service Management** - Modular service architecture

### File System
- Unix-style permissions (rwx)
- Inode-based file metadata
- LRU caching for performance
- Journal entries for crash recovery
- Virtual mount points
- Block device simulation

### Graphical Desktop Environment
- Modern Falcon theme (dark mode optimized)
- Window manager with compositing
- Taskbar with system tray
- Start menu with app launcher
- Built-in applications:
  - 📁 File Manager
  - ⚙️ Settings Panel
  - 🌐 Web Browser (simulated)
  - 📝 Text Editor
  - 💻 Terminal Emulator
  - 📊 System Monitor

### Performance Optimizations
- Multi-threaded subsystems
- Lock-free data structures where possible
- Efficient memory allocation
- File system caching
- Lazy loading of components

## 🚀 Installation & Usage

### Requirements
- Python 3.8+
- tkinter (for GUI mode) - usually pre-installed with Python

### Basic Usage

```bash
# Show system info
python3 main.py --info

# Run in CLI mode (default)
python3 main.py

# Run in headless mode (no GUI)
python3 main.py --headless

# Launch with GUI (requires tkinter)
python3 main.py --gui

# CLI only mode
python3 main.py --cli
```

### CLI Commands

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `ls` | List directory contents |
| `cat <file>` | Display file contents |
| `sysinfo` | Show system information |
| `ps` | List running processes |
| `clear` | Clear screen |
| `reboot` | Reboot system |
| `shutdown` | Shutdown system |
| `exit` | Exit CLI |

## 📁 Directory Structure

```
falcon_os_v2/
├── core/
│   └── kernel.py          # Main kernel with scheduler & memory manager
├── fs/
│   └── filesystem.py      # Virtual file system implementation
├── gui/
│   └── desktop.py         # Graphical desktop environment
├── utils/                  # Utility programs
├── bin/                    # System binaries
├── etc/                    # Configuration files
├── home/                   # User directories
├── var/                    # Variable data (logs, tmp)
└── main.py                 # System bootstrap entry point
```

## 🏗️ Architecture

### Kernel Subsystems

1. **Process Scheduler**
   - Multi-level feedback queue
   - 5 priority levels (CRITICAL to IDLE)
   - 10ms time quantum
   - Process states: READY, RUNNING, BLOCKED, TERMINATED

2. **Memory Manager**
   - Virtual memory with paging
   - 4KB page size
   - Per-process page tables
   - Dynamic allocation/deallocation

3. **System Call Handler**
   - Read/Write operations
   - File operations (open, close)
   - Process control (fork, exec, exit, wait)
   - Memory mapping (mmap, munmap)

### File System Layers

```
User Space
    ↓
System Call Interface
    ↓
VFS Layer (path resolution, permissions)
    ↓
Cache Layer (LRU caching)
    ↓
Journal Layer (crash recovery)
    ↓
Block Device (simulated storage)
```

## 🎨 Themes

The GUI supports three themes:

- **Falcon** (default) - Dark purple/blue theme optimized for productivity
- **Dark** - Classic dark mode
- **Light** - Light theme for bright environments

## 📊 System Information

Example output from `sysinfo`:

```json
{
  "name": "FalconOS",
  "version": "2.0.0-alpha",
  "build": 20241201,
  "codename": "Sky Hunter",
  "uptime_seconds": 123.45
}
```

File System stats:
- Total Files: Dynamic
- Total Directories: 22+
- Storage: 1024MB simulated

## 🔧 Development

### Running Tests

```bash
# Test kernel module
python3 core/kernel.py

# Test file system
python3 fs/filesystem.py

# Test GUI (requires display)
python3 gui/desktop.py
```

### Adding New Services

1. Create service class in `utils/`
2. Register in `_start_services()` method
3. Add to service list in main.py

### Extending the File System

```python
from fs.filesystem import get_filesystem

fs = get_filesystem()
fs.format("MY_VOLUME")
fs.mkdir("/my_directory")
fs.create_file("/my_directory/file.txt", content=b"Hello!")
content = fs.read_file("/my_directory/file.txt")
```

## 🛣️ Roadmap (v2.0 Beta)

- [ ] Network stack implementation
- [ ] Audio server
- [ ] Print spooler
- [ ] User authentication system
- [ ] Package manager
- [ ] Shell scripting support
- [ ] Device drivers framework
- [ ] Real-time clock integration
- [ ] Power management
- [ ] Internationalization (i18n)

## 📝 License

FalconOS is an educational/experimental operating system simulation.

## 🤝 Contributing

Contributions welcome! Areas needing help:
- Device driver simulations
- Network protocol implementations
- Additional shell commands
- GUI widgets and applications
- Performance optimizations

## 🦅 About the Name

"Falcon" represents speed, precision, and freedom - qualities we strive for in this operating system. The falcon is one of the fastest animals on Earth, capable of diving at over 240 mph, symbolizing our commitment to performance.

---

**Built with ❤️ by the FalconOS Team**

*Version 2.0.0 Alpha - December 2024*
