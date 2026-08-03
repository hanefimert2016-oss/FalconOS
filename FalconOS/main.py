#!/usr/bin/env python3
"""
FalconOS v2.1 Alpha - Ana Sistem Başlatıcı
Tüm kernel bileşenlerini başlatır ve sistem uygulamalarını çalıştırır
"""

import os
import sys
import time
import signal
import threading
from datetime import datetime

# Kernel modüllerini yükle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from kernel.core import SystemConfig, Logger, FalconError, VirtualFileSystem, MemoryManager
from kernel.process import ProcessManager, ProcessType, ProcessPriority

class FalconOS:
    """Ana FalconOS sınıfı"""
    
    def __init__(self):
        self.logger = Logger()
        self.running = False
        self.start_time = None
        
        # Kernel bileşenleri
        self.memory_manager = None
        self.file_system = None
        self.process_manager = None
        
        # Sistem servisleri
        self.services = {}
        
        self.logger.info(f"FalconOS v{SystemConfig.VERSION} '{SystemConfig.CODENAME}' başlatılıyor...")
    
    def boot(self):
        """Sistemi başlat"""
        boot_start = time.time()
        self.start_time = datetime.now()
        
        try:
            # 1. Bellek yöneticisini başlat
            self._init_memory()
            
            # 2. Dosya sistemini başlat
            self._init_filesystem()
            
            # 3. Process yöneticisini başlat
            self._init_process_manager()
            
            # 4. Sistem servislerini başlat
            self._init_services()
            
            # 5. Kullanıcı arayüzünü başlat
            self._init_ui()
            
            boot_time = time.time() - boot_start
            self.logger.info(f"Boot tamamlandı: {boot_time:.3f} saniye")
            
            self.running = True
            return True
            
        except Exception as e:
            self.logger.critical(f"Boot hatası: {e}")
            return False
    
    def _init_memory(self):
        """Bellek yöneticisini başlat"""
        self.logger.info("Bellek yöneticisi başlatılıyor...")
        # Daha küçük bellek ile başlat (performans için)
        self.memory_manager = MemoryManager(256)  # 256MB yerine daha az
        stats = self.memory_manager.get_memory_stats()
        self.logger.info(f"Bellek hazır: {stats['total_mb']:.0f}MB")
    
    def _init_filesystem(self):
        """Dosya sistemini başlat"""
        self.logger.info("Dosya sistemi başlatılıyor...")
        self.file_system = VirtualFileSystem()
        
        # Kayıtlı dosya sistemi var mı kontrol et
        fs_save_path = f"{SystemConfig.SYSTEM_ROOT}/data/filesystem.json"
        if os.path.exists(fs_save_path):
            try:
                self.file_system.load_from_disk(fs_save_path)
                self.logger.info("Dosya sistemi diskten yüklendi")
            except Exception as e:
                self.logger.warning(f"Dosya sistemi yüklenemedi: {e}")
        
        self.logger.info("Dosya sistemi hazır")
    
    def _init_process_manager(self):
        """Process yöneticisini başlat"""
        self.logger.info("Process yöneticisi başlatılıyor...")
        self.process_manager = ProcessManager(self.memory_manager)
        self.process_manager.scheduler.start()
        self.logger.info("Process yöneticisi hazır")
    
    def _init_services(self):
        """Sistem servislerini başlat"""
        self.logger.info("Sistem servisleri başlatılıyor...")
        
        # Network servisi (simülasyon)
        self.services['network'] = self._start_network_service()
        
        # Display servisi (simülasyon)
        self.services['display'] = self._start_display_service()
        
        # Input servisi (simülasyon)
        self.services['input'] = self._start_input_service()
        
        self.logger.info(f"{len(self.services)} servis başlatıldı")
    
    def _start_network_service(self) -> bool:
        """Network servisini başlat"""
        self.logger.debug("Network servisi başlatılıyor...")
        # Simüle edilmiş network
        return True
    
    def _start_display_service(self) -> bool:
        """Display servisini başlat"""
        self.logger.debug("Display servisi başlatılıyor...")
        # Simüle edilmiş display
        return True
    
    def _start_input_service(self) -> bool:
        """Input servisini başlat"""
        self.logger.debug("Input servisi başlatılıyor...")
        # Simüle edilmiş input
        return True
    
    def _init_ui(self):
        """Kullanıcı arayüzünü başlat"""
        self.logger.info("Kullanıcı arayüzü başlatılıyor...")
        
        # Hoş geldin mesajı
        print("\n" + "="*60)
        print(f"  FalconOS v{SystemConfig.VERSION} '{SystemConfig.CODENAME}'")
        print("="*60)
        print(f"  Build: {SystemConfig.BUILD_NUMBER}")
        print(f"  Boot Time: {self.start_time.strftime('%Y-%m-%d %H:%M:%S')}")
        print("="*60 + "\n")
    
    def run_command(self, command: str) -> str:
        """Terminal komutu çalıştır"""
        parts = command.strip().split()
        if not parts:
            return ""
        
        cmd = parts[0]
        args = parts[1:]
        
        if cmd == "help":
            return self._cmd_help()
        elif cmd == "ls":
            return self._cmd_ls(args)
        elif cmd == "cd":
            return self._cmd_cd(args)
        elif cmd == "cat":
            return self._cmd_cat(args)
        elif cmd == "mkdir":
            return self._cmd_mkdir(args)
        elif cmd == "touch":
            return self._cmd_touch(args)
        elif cmd == "rm":
            return self._cmd_rm(args)
        elif cmd == "ps":
            return self._cmd_ps()
        elif cmd == "top":
            return self._cmd_top()
        elif cmd == "mem":
            return self._cmd_mem()
        elif cmd == "tree":
            return self._cmd_tree()
        elif cmd == "save":
            return self._cmd_save()
        elif cmd == "clear":
            return "\033[2J\033[H"
        elif cmd == "version":
            return f"FalconOS v{SystemConfig.VERSION} '{SystemConfig.CODENAME}'"
        elif cmd == "exit":
            self.shutdown()
            return "Sistem kapatılıyor..."
        else:
            return f"Komut bulunamadı: {cmd}. 'help' yazın."
    
    def _cmd_help(self) -> str:
        return """
FalconOS Terminal Komutları:
  help          - Bu yardım mesajını göster
  ls [path]     - Dizin içeriğini listele
  cd <path>     - Dizin değiştir
  cat <file>    - Dosya içeriğini göster
  mkdir <path>  - Yeni dizin oluştur
  touch <file>  - Yeni dosya oluştur
  rm <path>     - Dosya/dizin sil
  ps            - Process'leri listele
  top           - Sistem istatistikleri
  mem           - Bellek kullanımı
  tree          - Dosya sistemi ağacı
  save          - Dosya sistemini kaydet
  clear         - Ekranı temizle
  version       - Sistem versiyonu
  exit          - Sistemi kapat
"""
    
    def _cmd_ls(self, args) -> str:
        path = args[0] if args else "/"
        try:
            items = self.file_system.list_dir(path)
            return "\n".join(items) if items else "(boş)"
        except FalconError as e:
            return f"Hata: {e.message}"
    
    def _cmd_cd(self, args) -> str:
        if not args:
            return "Kullanım: cd <path>"
        path = args[0]
        if self.file_system.exists(path):
            return f"Dizin değiştirildi: {path}"
        return f"Hata: Dizin bulunamadı: {path}"
    
    def _cmd_cat(self, args) -> str:
        if not args:
            return "Kullanım: cat <file>"
        path = args[0]
        try:
            content = self.file_system.read_file(path)
            return content.decode('utf-8', errors='replace')
        except FalconError as e:
            return f"Hata: {e.message}"
    
    def _cmd_mkdir(self, args) -> str:
        if not args:
            return "Kullanım: mkdir <path>"
        path = args[0]
        try:
            self.file_system.mkdir(path)
            return f"Dizin oluşturuldu: {path}"
        except FalconError as e:
            return f"Hata: {e.message}"
    
    def _cmd_touch(self, args) -> str:
        if not args:
            return "Kullanım: touch <file>"
        path = args[0]
        try:
            self.file_system.create_file(path, b"")
            return f"Dosya oluşturuldu: {path}"
        except FalconError as e:
            return f"Hata: {e.message}"
    
    def _cmd_rm(self, args) -> str:
        if not args:
            return "Kullanım: rm <path>"
        path = args[0]
        try:
            self.file_system.delete(path)
            return f"Silindi: {path}"
        except FalconError as e:
            return f"Hata: {e.message}"
    
    def _cmd_ps(self) -> str:
        procs = self.process_manager.list_processes()
        if not procs:
            return "(process yok)"
        
        lines = ["PID\tPPID\tNAME\tSTATE\tCPU(s)"]
        for p in procs:
            lines.append(f"{p['pid']}\t{p['ppid']}\t{p['name']}\t{p['state']}\t{p['cpu_time']:.2f}")
        return "\n".join(lines)
    
    def _cmd_top(self) -> str:
        stats = self.process_manager.get_stats()
        mem_stats = self.memory_manager.get_memory_stats()
        
        return f"""
=== FalconOS Sistem İstatistikleri ===
Process'ler: {stats['total_processes']}/{stats['max_processes']}
  - Running: {stats['states'].get('running', 0)}
  - Ready: {stats['states'].get('ready', 0)}
  - Terminated: {stats['states'].get('terminated', 0)}

Bellek:
  - Kullanılan: {mem_stats['used_mb']:.2f}MB / {mem_stats['total_mb']:.0f}MB
  - Kullanım: {mem_stats['usage_percent']:.1f}%

Zamanlayıcı:
  - Politika: {stats['scheduler']['policy']}
  - Context Switch: {stats['scheduler']['context_switches']}
  - Ready Queue: {stats['scheduler']['ready_queue_size']}

Thread'ler: {stats['threads']}
Toplam CPU Zamanı: {stats['total_cpu_time']:.2f}s
"""
    
    def _cmd_mem(self) -> str:
        stats = self.memory_manager.get_memory_stats()
        return f"""
Bellek İstatistikleri:
  Toplam: {stats['total_mb']:.0f}MB ({stats['total_pages']} sayfa)
  Kullanılan: {stats['used_mb']:.2f}MB ({stats['total_pages'] - stats['free_pages']} sayfa)
  Boş: {stats['free_mb']:.2f}MB ({stats['free_pages']} sayfa)
  Kullanım Oranı: {stats['usage_percent']:.1f}%
  Ayrılmış Process'ler: {stats['allocated_processes']}
"""
    
    def _cmd_tree(self) -> str:
        return self.file_system.get_tree("/", depth=4)
    
    def _cmd_save(self) -> str:
        try:
            os.makedirs(f"{SystemConfig.SYSTEM_ROOT}/data", exist_ok=True)
            fs_path = f"{SystemConfig.SYSTEM_ROOT}/data/filesystem.json"
            self.file_system.save_to_disk(fs_path)
            return f"Dosya sistemi kaydedildi: {fs_path}"
        except Exception as e:
            return f"Hata: {e}"
    
    def shutdown(self):
        """Sistemi kapat"""
        self.logger.info("Sistem kapatılıyor...")
        
        # Dosya sistemini kaydet
        try:
            os.makedirs(f"{SystemConfig.SYSTEM_ROOT}/data", exist_ok=True)
            self.file_system.save_to_disk(f"{SystemConfig.SYSTEM_ROOT}/data/filesystem.json")
        except Exception as e:
            self.logger.error(f"Dosya sistemi kaydedilemedi: {e}")
        
        # Process'leri sonlandır
        if self.process_manager:
            self.process_manager.scheduler.stop()
        
        self.running = False
        self.logger.info("Sistem kapatıldı")
    
    def run_interactive(self):
        """İnteraktif terminal modunu başlat"""
        print("\n🖥️  FalconOS Terminal'e hoş geldiniz!")
        print("Çıkmak için 'exit' yazın.\n")
        
        while self.running:
            try:
                cmd = input("user@falconos:~$ ").strip()
                if cmd:
                    result = self.run_command(cmd)
                    if result and result != "\033[2J\033[H":
                        print(result)
            except EOFError:
                break
            except KeyboardInterrupt:
                print("\n(CTRL+C yakalandı)")
            except Exception as e:
                print(f"Hata: {e}")
        
        print("\nGüle güle! 👋")

def main():
    """Ana fonksiyon"""
    os = FalconOS()
    
    if not os.boot():
        print("Boot başarısız!")
        sys.exit(1)
    
    # İnteraktif modu başlat
    os.run_interactive()

if __name__ == "__main__":
    main()
