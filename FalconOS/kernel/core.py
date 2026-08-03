#!/usr/bin/env python3
"""
FalconOS v2.1 Alpha - Core Kernel
Gerçek zamanlı görev yönetimi, bellek yönetimi ve sistem çağrıları
"""

import os
import sys
import time
import json
import threading
import queue
import signal
import hashlib
import pickle
from datetime import datetime
from typing import Dict, List, Optional, Any, Callable
from dataclasses import dataclass, field
from enum import Enum
import struct
import mmap
import ctypes
import subprocess
import shutil
import tempfile
import base64
import zlib

# ============================================================================
# SİSTEM SABİTLERİ VE YAPILANDIRMA
# ============================================================================

class SystemConfig:
    """Sistem yapılandırma sabitleri"""
    VERSION = "2.1.0-alpha"
    CODENAME = "Nexus"
    BUILD_NUMBER = 20240101
    MAX_PROCESSES = 256
    MAX_MEMORY_MB = 4096
    PAGE_SIZE = 4096
    MAX_OPEN_FILES = 1024
    MAX_USERS = 100
    SYSTEM_ROOT = "/workspace/FalconOS"
    VIRTUAL_FS_ROOT = "/falconsys"
    LOG_FILE = "/workspace/FalconOS/logs/kernel.log"
    CONFIG_FILE = "/workspace/FalconOS/config/system.json"
    
    # Renk kodları (Terminal için)
    COLORS = {
        'RESET': '\033[0m',
        'RED': '\033[91m',
        'GREEN': '\033[92m',
        'YELLOW': '\033[93m',
        'BLUE': '\033[94m',
        'MAGENTA': '\033[95m',
        'CYAN': '\033[96m',
        'WHITE': '\033[97m',
        'BOLD': '\033[1m',
        'UNDERLINE': '\033[4m'
    }

# ============================================================================
# HATA YÖNETİMİ VE LOG SİSTEMİ
# ============================================================================

class FalconError(Exception):
    """Temel FalconOS hata sınıfı"""
    def __init__(self, message: str, error_code: int = 0):
        self.message = message
        self.error_code = error_code
        self.timestamp = datetime.now()
        super().__init__(f"[{self.timestamp}] Error {error_code}: {message}")

class ErrorCode(Enum):
    """Hata kodları枚举"""
    SUCCESS = 0
    PERMISSION_DENIED = 1
    FILE_NOT_FOUND = 2
    MEMORY_FULL = 3
    PROCESS_LIMIT = 4
    INVALID_ARGUMENT = 5
    DEVICE_BUSY = 6
    NETWORK_ERROR = 7
    DISK_FULL = 8
    CORRUPTED_DATA = 9
    UNSUPPORTED_OPERATION = 10
    TIMEOUT = 11
    ACCESS_VIOLATION = 12
    NULL_POINTER = 13
    DIVISION_BY_ZERO = 14

class Logger:
    """Gelişmiş log sistemi"""
    _instance = None
    
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance.logs = []
            cls._instance.log_file = None
            cls._instance.lock = threading.Lock()
            cls._instance.setup_log_file()
        return cls._instance
    
    def setup_log_file(self):
        """Log dosyasını hazırla"""
        os.makedirs(os.path.dirname(SystemConfig.LOG_FILE), exist_ok=True)
        self.log_file = open(SystemConfig.LOG_FILE, 'a')
        self.log(f"Kernel başlatıldı - FalconOS v{SystemConfig.VERSION}")
    
    def log(self, message: str, level: str = "INFO"):
        """Log kaydı oluştur"""
        with self.lock:
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
            log_entry = f"[{timestamp}] [{level}] {message}"
            self.logs.append(log_entry)
            
            if self.log_file:
                self.log_file.write(log_entry + "\n")
                self.log_file.flush()
            
            # Konsola da yaz (renkli)
            color = SystemConfig.COLORS.get(level, SystemConfig.COLORS['WHITE'])
            print(f"{color}{log_entry}{SystemConfig.COLORS['RESET']}")
    
    def info(self, msg: str): self.log(msg, "INFO")
    def warning(self, msg: str): self.log(msg, "WARNING")
    def error(self, msg: str): self.log(msg, "ERROR")
    def debug(self, msg: str): self.log(msg, "DEBUG")
    def critical(self, msg: str): self.log(msg, "CRITICAL")
    
    def get_logs(self, limit: int = 100) -> List[str]:
        """Son logları getir"""
        return self.logs[-limit:]
    
    def close(self):
        """Log dosyasını kapat"""
        if self.log_file:
            self.log_file.close()

logger = Logger()

# ============================================================================
# BELLEK YÖNETİMİ
# ============================================================================

@dataclass
class MemoryPage:
    """Bellek sayfası"""
    page_id: int
    data: bytearray
    allocated: bool = False
    process_id: Optional[int] = None
    permissions: str = "rw-"  # read, write, execute
    
class MemoryManager:
    """Gelişmiş bellek yöneticisi"""
    def __init__(self, total_memory_mb: int = SystemConfig.MAX_MEMORY_MB):
        self.total_pages = (total_memory_mb * 1024 * 1024) // SystemConfig.PAGE_SIZE
        self.pages: List[MemoryPage] = []
        self.allocated_pages: Dict[int, List[int]] = {}  # PID -> [page_ids]
        self.free_list: List[int] = []
        self.lock = threading.Lock()
        
        # Belleği başlat
        for i in range(self.total_pages):
            page = MemoryPage(
                page_id=i,
                data=bytearray(SystemConfig.PAGE_SIZE)
            )
            self.pages.append(page)
            self.free_list.append(i)
        
        logger.info(f"Bellek yöneticisi başlatıldı: {total_memory_mb}MB, {self.total_pages} sayfa")
    
    def allocate_pages(self, pid: int, count: int) -> List[int]:
        """Process için bellek sayfaları ayır"""
        with self.lock:
            if len(self.free_list) < count:
                raise FalconError("Yetersiz bellek", ErrorCode.MEMORY_FULL.value)
            
            allocated = []
            for _ in range(count):
                page_id = self.free_list.pop(0)
                page = self.pages[page_id]
                page.allocated = True
                page.process_id = pid
                allocated.append(page_id)
            
            if pid not in self.allocated_pages:
                self.allocated_pages[pid] = []
            self.allocated_pages[pid].extend(allocated)
            
            logger.debug(f"PID {pid} için {count} sayfa ayrıldı")
            return allocated
    
    def free_pages(self, pid: int):
        """Process'in tüm belleğini serbest bırak"""
        with self.lock:
            if pid in self.allocated_pages:
                for page_id in self.allocated_pages[pid]:
                    page = self.pages[page_id]
                    page.allocated = False
                    page.process_id = None
                    page.data = bytearray(SystemConfig.PAGE_SIZE)
                    self.free_list.append(page_id)
                
                del self.allocated_pages[pid]
                logger.debug(f"PID {pid} belleği serbest bırakıldı")
    
    def read_page(self, page_id: int, offset: int = 0, size: int = None) -> bytes:
        """Sayfadan veri oku"""
        if page_id < 0 or page_id >= self.total_pages:
            raise FalconError("Geçersiz sayfa", ErrorCode.INVALID_ARGUMENT.value)
        
        page = self.pages[page_id]
        if size is None:
            size = SystemConfig.PAGE_SIZE - offset
        
        return bytes(page.data[offset:offset+size])
    
    def write_page(self, page_id: int, data: bytes, offset: int = 0):
        """Sayfaya veri yaz"""
        if page_id < 0 or page_id >= self.total_pages:
            raise FalconError("Geçersiz sayfa", ErrorCode.INVALID_ARGUMENT.value)
        
        page = self.pages[page_id]
        if not page.allocated:
            raise FalconError("Ayrılmamış sayfa", ErrorCode.ACCESS_VIOLATION.value)
        
        end_offset = offset + len(data)
        if end_offset > SystemConfig.PAGE_SIZE:
            raise FalconError("Sayfa sınırı aşıldı", ErrorCode.INVALID_ARGUMENT.value)
        
        page.data[offset:end_offset] = data
    
    def get_memory_stats(self) -> Dict[str, Any]:
        """Bellek istatistiklerini al"""
        with self.lock:
            total = len(self.pages)
            free = len(self.free_list)
            used = total - free
            
            return {
                'total_mb': (total * SystemConfig.PAGE_SIZE) / (1024 * 1024),
                'used_mb': (used * SystemConfig.PAGE_SIZE) / (1024 * 1024),
                'free_mb': (free * SystemConfig.PAGE_SIZE) / (1024 * 1024),
                'usage_percent': (used / total) * 100 if total > 0 else 0,
                'allocated_processes': len(self.allocated_pages),
                'total_pages': total,
                'free_pages': free
            }

# ============================================================================
# DOSYA SİSTEMİ
# ============================================================================

class FileType(Enum):
    FILE = "file"
    DIRECTORY = "directory"
    SYMLINK = "symlink"
    DEVICE = "device"
    SOCKET = "socket"

@dataclass
class FileNode:
    """Dosya sistemi düğümü"""
    name: str
    file_type: FileType
    parent: Optional['FileNode'] = None
    children: Dict[str, 'FileNode'] = field(default_factory=dict)
    content: bytes = b""
    metadata: Dict[str, Any] = field(default_factory=dict)
    created_at: datetime = field(default_factory=datetime.now)
    modified_at: datetime = field(default_factory=datetime.now)
    accessed_at: datetime = field(default_factory=datetime.now)
    permissions: str = "rw-r--r--"
    owner: str = "root"
    group: str = "users"
    size: int = 0
    inode: int = 0
    
    def __post_init__(self):
        if self.file_type == FileType.FILE:
            self.size = len(self.content)
        elif self.file_type == FileType.DIRECTORY:
            self.size = len(self.children)

class VirtualFileSystem:
    """Sanal dosya sistemi"""
    def __init__(self):
        self.root = FileNode("/", FileType.DIRECTORY)
        self.current_inode = 1
        self.mount_points: Dict[str, FileNode] = {}
        self.open_files: Dict[int, Dict] = {}  # fd -> {node, mode, position}
        self.fd_counter = 0
        self.lock = threading.Lock()
        
        # Temel dizinleri oluştur
        self._create_base_directories()
        logger.info("Sanal dosya sistemi başlatıldı")
    
    def _create_base_directories(self):
        """Temel sistem dizinlerini oluştur"""
        dirs = [
            "/bin", "/sbin", "/usr", "/usr/bin", "/usr/lib",
            "/home", "/home/user", "/tmp", "/var", "/var/log",
            "/etc", "/opt", "/mnt", "/media", "/proc", "/sys"
        ]
        
        for dir_path in dirs:
            self.mkdir(dir_path)
        
        # Bazı temel dosyaları oluştur
        self.create_file("/etc/version", SystemConfig.VERSION.encode())
        self.create_file("/etc/hostname", b"falconos")
        self.create_file("/home/user/readme.txt", 
            b"FalconOS v2.1 Nexus welcome!\nThis is a virtual file system.\n")
    
    def _get_node(self, path: str) -> Optional[FileNode]:
        """Yoldan düğüm bul"""
        if path == "/":
            return self.root
        
        parts = path.strip('/').split('/')
        current = self.root
        
        for part in parts:
            if part == "..":
                if current.parent:
                    current = current.parent
            elif part == ".":
                continue
            elif part in current.children:
                current = current.children[part]
            else:
                return None
        
        return current
    
    def _get_parent_and_name(self, path: str) -> tuple:
        """Ebeveyn düğüm ve isim döndür"""
        if path == "/":
            return None, ""
        
        parts = path.strip('/').split('/')
        name = parts[-1]
        parent_path = "/" + "/".join(parts[:-1]) if len(parts) > 1 else "/"
        
        parent = self._get_node(parent_path)
        return parent, name
    
    def exists(self, path: str) -> bool:
        """Dosya/dizin var mı?"""
        return self._get_node(path) is not None
    
    def mkdir(self, path: str, permissions: str = "rwxr-xr-x") -> bool:
        """Dizin oluştur"""
        with self.lock:
            if self.exists(path):
                return False
            
            parent, name = self._get_parent_and_name(path)
            if not parent or parent.file_type != FileType.DIRECTORY:
                raise FalconError("Geçersiz üst dizin", ErrorCode.PERMISSION_DENIED.value)
            
            node = FileNode(
                name=name,
                file_type=FileType.DIRECTORY,
                parent=parent,
                permissions=permissions,
                inode=self.current_inode
            )
            self.current_inode += 1
            
            parent.children[name] = node
            parent.modified_at = datetime.now()
            
            logger.debug(f"Dizin oluşturuldu: {path}")
            return True
    
    def create_file(self, path: str, content: bytes = b"", 
                   permissions: str = "rw-r--r--") -> bool:
        """Dosya oluştur"""
        with self.lock:
            if self.exists(path):
                return False
            
            parent, name = self._get_parent_and_name(path)
            if not parent or parent.file_type != FileType.DIRECTORY:
                raise FalconError("Geçersiz üst dizin", ErrorCode.PERMISSION_DENIED.value)
            
            node = FileNode(
                name=name,
                file_type=FileType.FILE,
                parent=parent,
                content=content,
                size=len(content),
                permissions=permissions,
                inode=self.current_inode
            )
            self.current_inode += 1
            
            parent.children[name] = node
            parent.modified_at = datetime.now()
            
            logger.debug(f"Dosya oluşturuldu: {path} ({len(content)} bayt)")
            return True
    
    def read_file(self, path: str) -> bytes:
        """Dosya oku"""
        node = self._get_node(path)
        if not node:
            raise FalconError("Dosya bulunamadı", ErrorCode.FILE_NOT_FOUND.value)
        
        if node.file_type != FileType.FILE:
            raise FalconError("Bu bir dosya değil", ErrorCode.UNSUPPORTED_OPERATION.value)
        
        node.accessed_at = datetime.now()
        return node.content
    
    def write_file(self, path: str, content: bytes) -> bool:
        """Dosya yaz"""
        node = self._get_node(path)
        if not node:
            # Dosya yoksa oluştur
            return self.create_file(path, content)
        
        if node.file_type != FileType.FILE:
            raise FalconError("Bu bir dosya değil", ErrorCode.UNSUPPORTED_OPERATION.value)
        
        with self.lock:
            node.content = content
            node.size = len(content)
            node.modified_at = datetime.now()
            
            logger.debug(f"Dosya yazıldı: {path} ({len(content)} bayt)")
            return True
    
    def append_file(self, path: str, content: bytes) -> bool:
        """Dosyaya ekle"""
        try:
            existing = self.read_file(path)
            return self.write_file(path, existing + content)
        except FalconError:
            return self.create_file(path, content)
    
    def delete(self, path: str) -> bool:
        """Dosya/dizin sil"""
        with self.lock:
            node = self._get_node(path)
            if not node:
                raise FalconError("Dosya bulunamadı", ErrorCode.FILE_NOT_FOUND.value)
            
            if node.file_type == FileType.DIRECTORY and node.children:
                raise FalconError("Dizin boş değil", ErrorCode.UNSUPPORTED_OPERATION.value)
            
            parent = node.parent
            if parent:
                del parent.children[node.name]
                parent.modified_at = datetime.now()
            
            logger.debug(f"Silindi: {path}")
            return True
    
    def list_dir(self, path: str) -> List[str]:
        """Dizin içeriğini listele"""
        node = self._get_node(path)
        if not node:
            raise FalconError("Dizin bulunamadı", ErrorCode.FILE_NOT_FOUND.value)
        
        if node.file_type != FileType.DIRECTORY:
            raise FalconError("Bu bir dizin değil", ErrorCode.UNSUPPORTED_OPERATION.value)
        
        node.accessed_at = datetime.now()
        return sorted(list(node.children.keys()))
    
    def get_file_info(self, path: str) -> Dict[str, Any]:
        """Dosya bilgilerini al"""
        node = self._get_node(path)
        if not node:
            raise FalconError("Dosya bulunamadı", ErrorCode.FILE_NOT_FOUND.value)
        
        return {
            'name': node.name,
            'type': node.file_type.value,
            'size': node.size,
            'permissions': node.permissions,
            'owner': node.owner,
            'group': node.group,
            'created': node.created_at.isoformat(),
            'modified': node.modified_at.isoformat(),
            'accessed': node.accessed_at.isoformat(),
            'inode': node.inode
        }
    
    def chmod(self, path: str, permissions: str) -> bool:
        """Dosya izinlerini değiştir"""
        node = self._get_node(path)
        if not node:
            raise FalconError("Dosya bulunamadı", ErrorCode.FILE_NOT_FOUND.value)
        
        with self.lock:
            node.permissions = permissions
            node.modified_at = datetime.now()
            return True
    
    def open(self, path: str, mode: str = "r") -> int:
        """Dosya aç, file descriptor döndür"""
        node = self._get_node(path)
        if not node:
            if "w" in mode or "a" in mode:
                self.create_file(path)
                node = self._get_node(path)
            else:
                raise FalconError("Dosya bulunamadı", ErrorCode.FILE_NOT_FOUND.value)
        
        with self.lock:
            self.fd_counter += 1
            fd = self.fd_counter
            
            position = 0
            if "a" in mode:
                position = node.size
            
            self.open_files[fd] = {
                'node': node,
                'mode': mode,
                'position': position
            }
            
            return fd
    
    def read_fd(self, fd: int, size: int = -1) -> bytes:
        """File descriptor'dan oku"""
        if fd not in self.open_files:
            raise FalconError("Geçersiz fd", ErrorCode.INVALID_ARGUMENT.value)
        
        file_info = self.open_files[fd]
        node = file_info['node']
        
        if "r" not in file_info['mode'] and "+" not in file_info['mode']:
            raise FalconError("Okuma izni yok", ErrorCode.PERMISSION_DENIED.value)
        
        position = file_info['position']
        
        if size == -1:
            data = node.content[position:]
            file_info['position'] = node.size
        else:
            data = node.content[position:position+size]
            file_info['position'] += len(data)
        
        node.accessed_at = datetime.now()
        return data
    
    def write_fd(self, fd: int, data: bytes) -> int:
        """File descriptor'a yaz"""
        if fd not in self.open_files:
            raise FalconError("Geçersiz fd", ErrorCode.INVALID_ARGUMENT.value)
        
        file_info = self.open_files[fd]
        node = file_info['node']
        
        if "w" not in file_info['mode'] and "a" not in file_info['mode'] and "+" not in file_info['mode']:
            raise FalconError("Yazma izni yok", ErrorCode.PERMISSION_DENIED.value)
        
        position = file_info['position']
        
        with self.lock:
            if "a" in file_info['mode']:
                node.content += data
                file_info['position'] = node.size
            else:
                node.content = node.content[:position] + data + node.content[position+len(data):]
                file_info['position'] += len(data)
            
            node.size = len(node.content)
            node.modified_at = datetime.now()
        
        return len(data)
    
    def close(self, fd: int):
        """File descriptor'ı kapat"""
        if fd in self.open_files:
            del self.open_files[fd]
    
    def save_to_disk(self, filepath: str):
        """Dosya sistemini diske kaydet"""
        def serialize_node(node: FileNode) -> Dict:
            return {
                'name': node.name,
                'type': node.file_type.value,
                'content': base64.b64encode(node.content).decode() if node.content else "",
                'metadata': node.metadata,
                'created_at': node.created_at.isoformat(),
                'modified_at': node.modified_at.isoformat(),
                'accessed_at': node.accessed_at.isoformat(),
                'permissions': node.permissions,
                'owner': node.owner,
                'group': node.group,
                'inode': node.inode,
                'children': {k: serialize_node(v) for k, v in node.children.items()}
            }
        
        data = serialize_node(self.root)
        with open(filepath, 'w') as f:
            json.dump(data, f, indent=2)
        
        logger.info(f"Dosya sistemi kaydedildi: {filepath}")
    
    def load_from_disk(self, filepath: str):
        """Dosya sistemini diskten yükle"""
        def deserialize_node(data: Dict, parent: Optional[FileNode] = None) -> FileNode:
            node = FileNode(
                name=data['name'],
                file_type=FileType(data['type']),
                parent=parent,
                content=base64.b64decode(data['content']) if data['content'] else b"",
                metadata=data.get('metadata', {}),
                created_at=datetime.fromisoformat(data['created_at']),
                modified_at=datetime.fromisoformat(data['modified_at']),
                accessed_at=datetime.fromisoformat(data['accessed_at']),
                permissions=data.get('permissions', "rw-r--r--"),
                owner=data.get('owner', "root"),
                group=data.get('group', "users"),
                inode=data.get('inode', 0)
            )
            
            if node.file_type == FileType.FILE:
                node.size = len(node.content)
            elif node.file_type == FileType.DIRECTORY:
                for child_name, child_data in data.get('children', {}).items():
                    child_node = deserialize_node(child_data, node)
                    node.children[child_name] = child_node
                node.size = len(node.children)
            
            return node
        
        with open(filepath, 'r') as f:
            data = json.load(f)
        
        self.root = deserialize_node(data)
        logger.info(f"Dosya sistemi yüklendi: {filepath}")
    
    def get_tree(self, path: str = "/", depth: int = 3, current_depth: int = 0) -> str:
        """Dosya sistemi ağacını string olarak döndür"""
        if current_depth >= depth:
            return ""
        
        node = self._get_node(path)
        if not node or node.file_type != FileType.DIRECTORY:
            return ""
        
        result = ""
        indent = "  " * current_depth
        
        if current_depth == 0:
            result += f"{indent}/\n"
        
        for name in sorted(node.children.keys()):
            child = node.children[name]
            if current_depth > 0 or name != "":  # Skip root duplicate
                prefix = "├── " if current_depth > 0 else ""
                suffix = "/" if child.file_type == FileType.DIRECTORY else ""
                result += f"{indent}{prefix}{name}{suffix}\n"
                
                if child.file_type == FileType.DIRECTORY:
                    result += self.get_tree(f"{path.rstrip('/')}/{name}", depth, current_depth + 1)
        
        return result

# Devam edecek... (Process Manager, Device Drivers, GUI, Applications vb.)
