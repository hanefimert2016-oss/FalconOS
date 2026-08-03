#!/usr/bin/env python3
"""
FalconOS v2.1 - Sistem Uygulamaları
Terminal, Dosya Yöneticisi, Ayarlar ve diğer sistem araçları
"""

import os
import sys
import time
import json
from datetime import datetime
from typing import Dict, List, Any

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from kernel.core import SystemConfig, FalconError

# ============================================================================
# TERMİNAL UYGULAMASI
# ============================================================================

class TerminalApp:
    """Gelişmiş Terminal Emülatörü"""
    
    def __init__(self, fs, pm, mm):
        self.fs = fs
        self.pm = pm
        self.mm = mm
        self.history = []
        self.current_dir = "/home/user"
        self.title = "Terminal"
    
    def run(self, args: List[str] = None):
        """Terminal'i başlat"""
        print(f"\n🖥️  FalconOS Terminal - {self.title}")
        print("=" * 50)
        
        while True:
            try:
                prompt = f"user@falconos:{self.current_dir}$ "
                cmd = input(prompt).strip()
                
                if not cmd:
                    continue
                
                self.history.append(cmd)
                result = self.execute(cmd)
                
                if result:
                    print(result)
                
                if cmd == "exit":
                    break
                    
            except EOFError:
                break
            except KeyboardInterrupt:
                print("\n(CTRL+C)")
    
    def execute(self, cmd_line: str) -> str:
        """Komut çalıştır"""
        parts = cmd_line.strip().split()
        if not parts:
            return ""
        
        cmd = parts[0]
        args = parts[1:]
        
        commands = {
            'help': self.cmd_help,
            'ls': self.cmd_ls,
            'cd': self.cmd_cd,
            'cat': self.cmd_cat,
            'pwd': self.cmd_pwd,
            'mkdir': self.cmd_mkdir,
            'touch': self.cmd_touch,
            'rm': self.cmd_rm,
            'cp': self.cmd_cp,
            'mv': self.cmd_mv,
            'echo': self.cmd_echo,
            'ps': self.cmd_ps,
            'kill': self.cmd_kill,
            'top': self.cmd_top,
            'mem': self.cmd_mem,
            'tree': self.cmd_tree,
            'clear': lambda: "\033[2J\033[H",
            'history': self.cmd_history,
            'date': self.cmd_date,
            'whoami': self.cmd_whoami,
            'uname': self.cmd_uname,
            'df': self.cmd_df,
            'free': self.cmd_free,
            'reboot': self.cmd_reboot,
        }
        
        if cmd in commands:
            try:
                return commands[cmd](args)
            except FalconError as e:
                return f"❌ Hata: {e.message}"
            except Exception as e:
                return f"❌ Beklenmeyen hata: {e}"
        else:
            return f"❌ Komut bulunamadı: {cmd}. 'help' yazın."
    
    def cmd_help(self, args) -> str:
        return """
📚 FalconOS Terminal Komutları:

📁 Dosya İşlemleri:
  ls [path]       - Dizin içeriğini listele
  cd <path>       - Dizin değiştir
  pwd             - Çalışma dizinini göster
  cat <file>      - Dosya içeriğini göster
  mkdir <path>    - Yeni dizin oluştur
  touch <file>    - Yeni dosya oluştur
  rm <path>       - Dosya/dizin sil
  cp <src> <dst>  - Dosya kopyala
  mv <src> <dst>  - Dosya taşı/yeniden adlandır

💻 Sistem Komutları:
  ps              - Process'leri listele
  kill <pid>      - Process sonlandır
  top             - Sistem istatistikleri
  mem             - Bellek kullanımı
  df              - Disk kullanımı
  free            - Serbest bellek
  tree            - Dosya sistemi ağacı

ℹ️  Bilgi Komutları:
  help            - Bu yardım mesajı
  history         - Komut geçmişi
  date            - Tarih ve saat
  whoami          - Kullanıcı adı
  uname           - Sistem bilgisi

🔧 Diğer:
  clear           - Ekranı temizle
  echo <text>     - Metni yazdır
  reboot          - Sistemi yeniden başlat
  exit            - Terminal'den çık
"""
    
    def cmd_ls(self, args) -> str:
        path = args[0] if args else self.current_dir
        items = self.fs.list_dir(path)
        if not items:
            return "(boş dizin)"
        
        result = []
        for item in items:
            full_path = f"{path.rstrip('/')}/{item}"
            info = self.fs.get_file_info(full_path)
            icon = "📁" if info['type'] == 'directory' else "📄"
            result.append(f"{icon} {item}")
        
        return "\n".join(result)
    
    def cmd_cd(self, args) -> str:
        if not args:
            self.current_dir = "/home/user"
            return ""
        
        path = args[0]
        if path == "..":
            parts = self.current_dir.rstrip('/').split('/')
            if len(parts) > 1:
                self.current_dir = '/'.join(parts[:-1]) or '/'
        elif path.startswith('/'):
            if self.fs.exists(path):
                self.current_dir = path
            else:
                raise FalconError(f"Dizin bulunamadı: {path}", 2)
        else:
            full_path = f"{self.current_dir.rstrip('/')}/{path}"
            if self.fs.exists(full_path):
                self.current_dir = full_path
            else:
                raise FalconError(f"Dizin bulunamadı: {path}", 2)
        
        return ""
    
    def cmd_pwd(self, args) -> str:
        return self.current_dir
    
    def cmd_cat(self, args) -> str:
        if not args:
            return "Kullanım: cat <dosya>"
        
        path = args[0]
        if not path.startswith('/'):
            path = f"{self.current_dir.rstrip('/')}/{path}"
        
        content = self.fs.read_file(path)
        return content.decode('utf-8', errors='replace')
    
    def cmd_mkdir(self, args) -> str:
        if not args:
            return "Kullanım: mkdir <dizin>"
        
        path = args[0]
        if not path.startswith('/'):
            path = f"{self.current_dir.rstrip('/')}/{path}"
        
        self.fs.mkdir(path)
        return f"✅ Dizin oluşturuldu: {path}"
    
    def cmd_touch(self, args) -> str:
        if not args:
            return "Kullanım: touch <dosya>"
        
        path = args[0]
        if not path.startswith('/'):
            path = f"{self.current_dir.rstrip('/')}/{path}"
        
        self.fs.create_file(path, b"")
        return f"✅ Dosya oluşturuldu: {path}"
    
    def cmd_rm(self, args) -> str:
        if not args:
            return "Kullanım: rm <dosya>"
        
        path = args[0]
        if not path.startswith('/'):
            path = f"{self.current_dir.rstrip('/')}/{path}"
        
        self.fs.delete(path)
        return f"✅ Silindi: {path}"
    
    def cmd_cp(self, args) -> str:
        if len(args) < 2:
            return "Kullanım: cp <kaynak> <hedef>"
        
        src, dst = args[0], args[1]
        content = self.fs.read_file(src)
        self.fs.write_file(dst, content)
        return f"✅ Kopyalandı: {src} -> {dst}"
    
    def cmd_mv(self, args) -> str:
        if len(args) < 2:
            return "Kullanım: mv <kaynak> <hedef>"
        
        src, dst = args[0], args[1]
        content = self.fs.read_file(src)
        self.fs.delete(src)
        self.fs.write_file(dst, content)
        return f"✅ Taşındı: {src} -> {dst}"
    
    def cmd_echo(self, args) -> str:
        return " ".join(args)
    
    def cmd_ps(self, args) -> str:
        procs = self.pm.list_processes()
        if not procs:
            return "(aktif process yok)"
        
        lines = ["PID\tPPID\tNAME\tSTATE\tCPU(s)"]
        for p in procs:
            lines.append(f"{p['pid']}\t{p['ppid']}\t{p['name']}\t{p['state']}\t{p['cpu_time']:.2f}")
        return "\n".join(lines)
    
    def cmd_kill(self, args) -> str:
        if not args:
            return "Kullanım: kill <PID>"
        
        pid = int(args[0])
        if self.pm.kill(pid):
            return f"✅ Process sonlandırıldı: {pid}"
        return f"❌ Process bulunamadı: {pid}"
    
    def cmd_top(self, args) -> str:
        stats = self.pm.get_stats()
        mem_stats = self.mm.get_memory_stats()
        
        return f"""
╔══════════════════════════════════════════╗
║     📊 FalconOS Sistem İstatistikleri    ║
╠══════════════════════════════════════════╣
║ Process'ler: {stats['total_processes']:>3}/{stats['max_processes']}                  ║
║   Running: {stats['states'].get('running', 0):>2}  Ready: {stats['states'].get('ready', 0):>2}  Terminated: {stats['states'].get('terminated', 0):>2}     ║
╠══════════════════════════════════════════╣
║ Bellek: {mem_stats['used_mb']:>6.2f}MB / {mem_stats['total_mb']:>6.0f}MB ({mem_stats['usage_percent']:>5.1f}%)     ║
╠══════════════════════════════════════════╣
║ Zamanlayıcı: {stats['scheduler']['policy']:>10}                 ║
║ Context Switch: {stats['scheduler']['context_switches']:>5}            ║
║ Thread'ler: {stats['threads']:>3}                       ║
╚══════════════════════════════════════════╝
"""
    
    def cmd_mem(self, args) -> str:
        stats = self.mm.get_memory_stats()
        return f"""
╔══════════════════════════════════════════╗
║        💾 Bellek İstatistikleri          ║
╠══════════════════════════════════════════╣
║ Toplam:     {stats['total_mb']:>8.0f} MB  ({stats['total_pages']:>8} sayfa)  ║
║ Kullanılan: {stats['used_mb']:>8.2f} MB  ({stats['total_pages'] - stats['free_pages']:>8} sayfa)  ║
║ Boş:        {stats['free_mb']:>8.2f} MB  ({stats['free_pages']:>8} sayfa)  ║
║ Kullanım:   {stats['usage_percent']:>8.1f} %                   ║
║ Process'ler: {stats['allocated_processes']:>8}                   ║
╚══════════════════════════════════════════╝
"""
    
    def cmd_tree(self, args) -> str:
        depth = int(args[0]) if args else 3
        return self.fs.get_tree("/", depth=depth)
    
    def cmd_history(self, args) -> str:
        if not self.history:
            return "(geçmiş boş)"
        return "\n".join([f"{i+1}. {cmd}" for i, cmd in enumerate(self.history[-20:])])
    
    def cmd_date(self, args) -> str:
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    def cmd_whoami(self, args) -> str:
        return "user"
    
    def cmd_uname(self, args) -> str:
        if args and args[0] == "-a":
            return f"FalconOS {SystemConfig.VERSION} {SystemConfig.CODENAME} Python {sys.version.split()[0]}"
        return f"FalconOS"
    
    def cmd_df(self, args) -> str:
        return """
Filesystem      Size    Used    Avail   Use%    Mounted on
/dev/sda1       256MB   12MB    244MB   5%      /
/dev/sda2       512MB   0MB     512MB   0%      /home
"""
    
    def cmd_free(self, args) -> str:
        stats = self.mm.get_memory_stats()
        used_kb = int(stats['used_mb'] * 1024)
        total_kb = int(stats['total_mb'] * 1024)
        free_kb = int(stats['free_mb'] * 1024)
        
        return f"""
              total        used        free
Mem:        {total_kb:>10}     {used_kb:>10}     {free_kb:>10}
Swap:              0           0           0
"""
    
    def cmd_reboot(self, args) -> str:
        return "🔄 Sistem yeniden başlatılıyor..."

# ============================================================================
# DOSYA YÖNETİCİSİ
# ============================================================================

class FileManagerApp:
    """Grafiksel Dosya Yöneticisi (Simülasyon)"""
    
    def __init__(self, fs):
        self.fs = fs
        self.current_path = "/home/user"
        self.view_mode = "icons"  # icons, list, details
        self.selected = []
    
    def run(self, args: List[str] = None):
        """Dosya yöneticisini başlat"""
        print("\n📁 FalconOS Dosya Yöneticisi")
        print("=" * 50)
        self._render()
    
    def _render(self):
        """Arayüzü render et"""
        items = self.fs.list_dir(self.current_path)
        
        print(f"\n📂 {self.current_path}")
        print("-" * 40)
        
        if self.view_mode == "icons":
            # Icon görünümü
            cols = 4
            for i, item in enumerate(items):
                full_path = f"{self.current_path.rstrip('/')}/{item}"
                info = self.fs.get_file_info(full_path)
                icon = "📁" if info['type'] == 'directory' else "📄"
                print(f"{icon} {item:<15}", end="")
                if (i + 1) % cols == 0:
                    print()
            print()
        elif self.view_mode == "list":
            # Liste görünümü
            for item in items:
                full_path = f"{self.current_path.rstrip('/')}/{item}"
                info = self.fs.get_file_info(full_path)
                icon = "📁" if info['type'] == 'directory' else "📄"
                print(f"{icon} {item:<20} {info['size']:>8} bytes")
        else:  # details
            # Detaylı görünümü
            print(f"{'Name':<25} {'Type':<10} {'Size':>10} {'Modified':<20}")
            print("-" * 70)
            for item in items:
                full_path = f"{self.current_path.rstrip('/')}/{item}"
                info = self.fs.get_file_info(full_path)
                modified = info['modified'][:16].replace('T', ' ')
                print(f"{item:<25} {info['type']:<10} {info['size']:>10} {modified:<20}")
        
        print("\nKomutlar: [O]pen  [D]elete  [R]ename  [N]ew  [V]iew  [Q]uit")

# ============================================================================
# AYARLAR UYGULAMASI
# ============================================================================

class SettingsApp:
    """Sistem Ayarları"""
    
    def __init__(self):
        self.settings = {
            'display': {
                'resolution': '1920x1080',
                'brightness': 80,
                'theme': 'dark'
            },
            'sound': {
                'volume': 70,
                'mute': False
            },
            'network': {
                'wifi': True,
                'bluetooth': False
            },
            'system': {
                'language': 'tr',
                'timezone': 'Europe/Istanbul'
            }
        }
    
    def run(self, args: List[str] = None):
        """Ayarları göster"""
        print("\n⚙️  FalconOS Sistem Ayarları")
        print("=" * 50)
        
        categories = ['display', 'sound', 'network', 'system']
        
        for category in categories:
            print(f"\n📌 {category.upper()}")
            print("-" * 30)
            for key, value in self.settings[category].items():
                status = "✅" if value else "❌" if isinstance(value, bool) else ""
                print(f"  {key}: {value} {status}")
        
        print("\n[D]eğiştir  [S]ave  [R]eset  [Q]uit")

# ============================================================================
# WEB TARAYICI (Simülasyon)
# ============================================================================

class BrowserApp:
    """Web Tarayıcı (Simülasyon)"""
    
    def __init__(self):
        self.homepage = "https://falconos.local"
        self.history = []
        self.bookmarks = []
    
    def run(self, args: List[str] = None):
        """Tarayıcıyı başlat"""
        url = args[0] if args else self.homepage
        print(f"\n🌐 FalconOS Web Tarayıcı")
        print("=" * 50)
        print(f"URL: {url}")
        print("\n[Sayfa yükleniyor...]")
        print("\n🏠 Ana Sayfa - FalconOS")
        print("   Hoş geldiniz! Bu bir simülasyondur.")
        print("\n[B]ack  [F]orward  [R]eload  [H]istory  [Q]uit")

# ============================================================================
# UYGULAMA LAUNCHER
# ============================================================================

class AppLauncher:
    """Uygulama Başlatıcı"""
    
    def __init__(self, fs, pm, mm):
        self.apps = {
            'terminal': lambda: TerminalApp(fs, pm, mm),
            'files': lambda: FileManagerApp(fs),
            'settings': lambda: SettingsApp(),
            'browser': lambda: BrowserApp(),
        }
        self.installed = list(self.apps.keys())
    
    def list_apps(self) -> str:
        """Yüklü uygulamaları listele"""
        icons = {
            'terminal': '🖥️',
            'files': '📁',
            'settings': '⚙️',
            'browser': '🌐'
        }
        
        result = ["\n📱 FalconOS Uygulamaları:", "=" * 30]
        for app in self.installed:
            icon = icons.get(app, '📦')
            result.append(f"{icon} {app}")
        return "\n".join(result)
    
    def launch(self, app_name: str, args: List[str] = None):
        """Uygulama başlat"""
        if app_name not in self.apps:
            print(f"❌ Uygulama bulunamadı: {app_name}")
            return False
        
        print(f"\n🚀 {app_name} başlatılıyor...")
        app = self.apps[app_name]()
        app.run(args)
        return True

if __name__ == "__main__":
    # Test için basit çalıştırma
    print("FalconOS Uygulamaları Test")
    print("=" * 30)
    
    from kernel.core import VirtualFileSystem, MemoryManager
    from kernel.process import ProcessManager
    
    fs = VirtualFileSystem()
    mm = MemoryManager(128)
    pm = ProcessManager(mm)
    
    launcher = AppLauncher(fs, pm, mm)
    print(launcher.list_apps())
