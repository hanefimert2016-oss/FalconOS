#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FalconOS v2.0 Alpha - Advanced Virtual File System
Journaling file system with permissions, caching, and virtual mounts
"""

import os
import json
import time
import hashlib
import threading
import pickle
from datetime import datetime
from typing import Dict, List, Optional, Any, Tuple, Set
from dataclasses import dataclass, field
from enum import Enum
from pathlib import PurePosixPath
import struct

class FileType(Enum):
    FILE = "file"
    DIRECTORY = "directory"
    SYMLINK = "symlink"
    DEVICE = "device"
    SOCKET = "socket"
    FIFO = "fifo"

class Permission(Enum):
    READ = 0b100
    WRITE = 0b010
    EXECUTE = 0b001

@dataclass
class Inode:
    """File system inode - stores metadata about files/directories"""
    inode_id: int
    file_type: FileType
    permissions: int  # Unix-style permissions (e.g., 0o755)
    owner_uid: int
    group_gid: int
    size: int = 0
    created_at: float = field(default_factory=time.time)
    modified_at: float = field(default_factory=time.time)
    accessed_at: float = field(default_factory=time.time)
    link_count: int = 1
    block_pointers: List[int] = field(default_factory=list)  # Pointers to data blocks
    symlink_target: Optional[str] = None
    extended_attrs: Dict[str, str] = field(default_factory=dict)
    
    def to_dict(self) -> Dict[str, Any]:
        return {
            "inode_id": self.inode_id,
            "file_type": self.file_type.value,
            "permissions": oct(self.permissions),
            "owner_uid": self.owner_uid,
            "group_gid": self.group_gid,
            "size": self.size,
            "created_at": datetime.fromtimestamp(self.created_at).isoformat(),
            "modified_at": datetime.fromtimestamp(self.modified_at).isoformat(),
            "accessed_at": datetime.fromtimestamp(self.accessed_at).isoformat(),
            "link_count": self.link_count,
            "block_pointers": self.block_pointers,
            "symlink_target": self.symlink_target,
            "extended_attrs": self.extended_attrs
        }

@dataclass
class DirectoryEntry:
    """Directory entry mapping name to inode"""
    name: str
    inode_id: int
    entry_type: FileType

@dataclass
class JournalEntry:
    """Journal entry for file system recovery"""
    transaction_id: int
    timestamp: float
    operation: str  # CREATE, DELETE, MODIFY, RENAME
    path: str
    old_data: Optional[bytes] = None
    new_data: Optional[bytes] = None
    old_inode: Optional[Dict] = None
    new_inode: Optional[Dict] = None
    committed: bool = False

class BlockDevice:
    """Simulated block device for storing file system data"""
    
    def __init__(self, size_mb: int = 1024, block_size: int = 4096):
        self.size_bytes = size_mb * 1024 * 1024
        self.block_size = block_size
        self.num_blocks = self.size_bytes // block_size
        self.blocks: Dict[int, bytes] = {}
        self._lock = threading.Lock()
        
    def read_block(self, block_num: int) -> bytes:
        with self._lock:
            if block_num < 0 or block_num >= self.num_blocks:
                raise ValueError(f"Invalid block number: {block_num}")
            return self.blocks.get(block_num, b'\x00' * self.block_size)
    
    def write_block(self, block_num: int, data: bytes):
        with self._lock:
            if block_num < 0 or block_num >= self.num_blocks:
                raise ValueError(f"Invalid block number: {block_num}")
            if len(data) > self.block_size:
                data = data[:self.block_size]
            elif len(data) < self.block_size:
                data = data + b'\x00' * (self.block_size - len(data))
            self.blocks[block_num] = data
    
    def allocate_blocks(self, count: int) -> List[int]:
        with self._lock:
            available = [i for i in range(self.num_blocks) if i not in self.blocks]
            if len(available) < count:
                raise MemoryError("Not enough space on device")
            allocated = available[:count]
            for block in allocated:
                self.blocks[block] = b'\x00' * self.block_size
            return allocated
    
    def free_blocks(self, block_nums: List[int]):
        with self._lock:
            for block in block_nums:
                if block in self.blocks:
                    del self.blocks[block]
    
    def get_stats(self) -> Dict[str, Any]:
        with self._lock:
            used_blocks = len(self.blocks)
            return {
                "total_size_mb": self.size_bytes / (1024 * 1024),
                "used_size_mb": (used_blocks * self.block_size) / (1024 * 1024),
                "free_size_mb": ((self.num_blocks - used_blocks) * self.block_size) / (1024 * 1024),
                "block_size": self.block_size,
                "total_blocks": self.num_blocks,
                "used_blocks": used_blocks,
                "free_blocks": self.num_blocks - used_blocks
            }

class FileSystemCache:
    """LRU cache for frequently accessed files and directories"""
    
    def __init__(self, max_size: int = 1000):
        self.max_size = max_size
        self.cache: Dict[str, Tuple[Any, float]] = {}
        self._lock = threading.Lock()
        
    def get(self, key: str) -> Optional[Any]:
        with self._lock:
            if key in self.cache:
                value, _ = self.cache[key]
                # Update access time
                self.cache[key] = (value, time.time())
                return value
            return None
    
    def put(self, key: str, value: Any):
        with self._lock:
            if key in self.cache:
                self.cache[key] = (value, time.time())
            else:
                if len(self.cache) >= self.max_size:
                    # Remove LRU entry
                    oldest_key = min(self.cache.keys(), key=lambda k: self.cache[k][1])
                    del self.cache[oldest_key]
                self.cache[key] = (value, time.time())
    
    def invalidate(self, key: str):
        with self._lock:
            if key in self.cache:
                del self.cache[key]
    
    def clear(self):
        with self._lock:
            self.cache.clear()

class VirtualFileSystem:
    """Main virtual file system implementation"""
    
    BLOCK_SIZE = 4096
    MAX_PATH_LENGTH = 4096
    MAX_FILENAME_LENGTH = 255
    
    def __init__(self, device: Optional[BlockDevice] = None):
        self.device = device or BlockDevice(size_mb=1024)
        self.inodes: Dict[int, Inode] = {}
        self.directory_entries: Dict[int, Dict[str, DirectoryEntry]] = {}  # inode_id -> {name: entry}
        self.next_inode_id = 1
        self.mount_points: Dict[str, int] = {}  # path -> root_inode_id
        self.open_files: Dict[int, Dict[str, Any]] = {}  # fd -> file info
        self.next_fd = 3  # 0,1,2 reserved for stdin,stdout,stderr
        self.journal: List[JournalEntry] = []
        self.cache = FileSystemCache()
        self._lock = threading.RLock()
        self.initialized = False
        
    def format(self, volume_label: str = "FALCON_ROOT"):
        """Format the file system"""
        with self._lock:
            # Create root directory
            root_inode = Inode(
                inode_id=self.next_inode_id,
                file_type=FileType.DIRECTORY,
                permissions=0o755,
                owner_uid=0,
                group_gid=0
            )
            self.next_inode_id += 1
            
            self.inodes[root_inode.inode_id] = root_inode
            self.directory_entries[root_inode.inode_id] = {}
            
            # Create standard directories
            standard_dirs = ["bin", "etc", "home", "var", "tmp", "dev", "proc", "sys", "usr", "opt"]
            for dir_name in standard_dirs:
                self._create_entry(root_inode.inode_id, dir_name, FileType.DIRECTORY, 0o755)
            
            # Mount root
            self.mount_points["/"] = root_inode.inode_id
            
            # Write superblock
            superblock = {
                "version": "2.0.0-alpha",
                "volume_label": volume_label,
                "created_at": datetime.now().isoformat(),
                "block_size": self.BLOCK_SIZE,
                "root_inode": root_inode.inode_id
            }
            self._write_superblock(superblock)
            
            self.initialized = True
            print(f"[FS] Formatted volume: {volume_label}")
            print(f"[FS] Root inode: {root_inode.inode_id}")
            
    def _write_superblock(self, superblock: Dict):
        """Write superblock to first block"""
        data = json.dumps(superblock).encode('utf-8')
        self.device.write_block(0, data)
    
    def _read_superblock(self) -> Optional[Dict]:
        """Read superblock from first block"""
        data = self.device.read_block(0)
        if data.strip(b'\x00'):
            try:
                return json.loads(data.decode('utf-8'))
            except:
                pass
        return None
    
    def _resolve_path(self, path: str, follow_symlinks: bool = True) -> Tuple[int, str]:
        """Resolve path to (inode_id, filename)"""
        if not path.startswith('/'):
            # Relative path resolution would go here
            raise ValueError("Only absolute paths supported in this version")
        
        parts = PurePosixPath(path).parts
        if len(parts) == 1 and parts[0] == '/':
            return self.mount_points["/"], ""
        
        current_inode = self.mount_points["/"]
        
        for i, part in enumerate(parts[1:], 1):
            if part == '.':
                continue
            elif part == '..':
                # Navigate to parent (simplified)
                continue
            
            if current_inode not in self.directory_entries:
                raise FileNotFoundError(f"Path not found: {path}")
            
            dir_entries = self.directory_entries[current_inode]
            if part not in dir_entries:
                raise FileNotFoundError(f"Path not found: {path}")
            
            entry = dir_entries[part]
            current_inode = entry.inode_id
            
            # Follow symlinks if requested
            if follow_symlinks and entry.entry_type == FileType.SYMLINK:
                inode = self.inodes[entry.inode_id]
                target_path = inode.symlink_target
                return self._resolve_path(target_path, follow_symlinks=True)
        
        filename = parts[-1] if parts[-1] not in ['.', '..'] else ""
        return current_inode, filename
    
    def _create_entry(self, parent_inode_id: int, name: str, file_type: FileType, 
                     permissions: int = 0o644) -> Inode:
        """Create a new file/directory entry"""
        if len(name) > self.MAX_FILENAME_LENGTH:
            raise ValueError(f"Filename too long: {name}")
        
        inode = Inode(
            inode_id=self.next_inode_id,
            file_type=file_type,
            permissions=permissions,
            owner_uid=0,
            group_gid=0
        )
        self.next_inode_id += 1
        
        self.inodes[inode.inode_id] = inode
        
        if file_type == FileType.DIRECTORY:
            self.directory_entries[inode.inode_id] = {}
        
        # Add to parent directory
        if parent_inode_id in self.directory_entries:
            entry = DirectoryEntry(name=name, inode_id=inode.inode_id, entry_type=file_type)
            self.directory_entries[parent_inode_id][name] = entry
            
            # Update parent mtime
            self.inodes[parent_inode_id].modified_at = time.time()
        
        # Log journal entry
        self._journal_write(JournalEntry(
            transaction_id=len(self.journal),
            timestamp=time.time(),
            operation="CREATE",
            path=f"/{name}",
            new_inode=inode.to_dict()
        ))
        
        return inode
    
    def mkdir(self, path: str, permissions: int = 0o755) -> int:
        """Create a directory"""
        with self._lock:
            parent_path = str(PurePosixPath(path).parent)
            dir_name = PurePosixPath(path).name
            
            if parent_path == ".":
                parent_inode = self.mount_points["/"]
            else:
                parent_inode, _ = self._resolve_path(parent_path)
            
            if parent_inode not in self.directory_entries:
                raise FileNotFoundError(f"Parent directory not found: {parent_path}")
            
            if dir_name in self.directory_entries[parent_inode]:
                raise FileExistsError(f"Directory already exists: {path}")
            
            inode = self._create_entry(parent_inode, dir_name, FileType.DIRECTORY, permissions)
            self.cache.invalidate(parent_path)
            return inode.inode_id
    
    def create_file(self, path: str, permissions: int = 0o644, content: bytes = b"") -> int:
        """Create a file"""
        with self._lock:
            parent_path = str(PurePosixPath(path).parent)
            file_name = PurePosixPath(path).name
            
            if parent_path == ".":
                parent_inode = self.mount_points["/"]
            else:
                parent_inode, _ = self._resolve_path(parent_path)
            
            inode = self._create_entry(parent_inode, file_name, FileType.FILE, permissions)
            
            if content:
                self.write_file(path, content)
            
            self.cache.invalidate(parent_path)
            return inode.inode_id
    
    def write_file(self, path: str, content: bytes) -> int:
        """Write content to a file"""
        with self._lock:
            inode_id, _ = self._resolve_path(path)
            
            if inode_id not in self.inodes:
                raise FileNotFoundError(f"File not found: {path}")
            
            inode = self.inodes[inode_id]
            if inode.file_type != FileType.FILE:
                raise IsADirectoryError(f"Not a file: {path}")
            
            # Calculate required blocks
            num_blocks = (len(content) + self.BLOCK_SIZE - 1) // self.BLOCK_SIZE
            
            # Free old blocks
            if inode.block_pointers:
                self.device.free_blocks(inode.block_pointers)
            
            # Allocate new blocks
            inode.block_pointers = self.device.allocate_blocks(num_blocks)
            
            # Write data to blocks
            for i, block_num in enumerate(inode.block_pointers):
                start = i * self.BLOCK_SIZE
                end = start + self.BLOCK_SIZE
                block_data = content[start:end]
                self.device.write_block(block_num, block_data)
            
            # Update inode
            inode.size = len(content)
            inode.modified_at = time.time()
            
            # Cache invalidation
            self.cache.invalidate(path)
            
            return len(content)
    
    def read_file(self, path: str, offset: int = 0, size: int = -1) -> bytes:
        """Read content from a file"""
        with self._lock:
            # Check cache first
            cached = self.cache.get(f"{path}:{offset}:{size}")
            if cached:
                return cached
            
            inode_id, _ = self._resolve_path(path)
            
            if inode_id not in self.inodes:
                raise FileNotFoundError(f"File not found: {path}")
            
            inode = self.inodes[inode_id]
            if inode.file_type != FileType.FILE:
                raise IsADirectoryError(f"Not a file: {path}")
            
            if size == -1:
                size = inode.size - offset
            
            # Read data from blocks
            result = bytearray()
            start_block = offset // self.BLOCK_SIZE
            end_offset = offset + size
            end_block = (end_offset + self.BLOCK_SIZE - 1) // self.BLOCK_SIZE
            
            for block_num in inode.block_pointers[start_block:end_block+1]:
                block_data = self.device.read_block(block_num)
                result.extend(block_data)
            
            result = bytes(result[offset % self.BLOCK_SIZE:offset % self.BLOCK_SIZE + size])
            
            # Update access time
            inode.accessed_at = time.time()
            
            # Cache result
            self.cache.put(f"{path}:{offset}:{size}", result)
            
            return result
    
    def open(self, path: str, flags: int = 0) -> int:
        """Open a file and return file descriptor"""
        with self._lock:
            inode_id, _ = self._resolve_path(path)
            
            fd = self.next_fd
            self.next_fd += 1
            
            self.open_files[fd] = {
                "path": path,
                "inode_id": inode_id,
                "flags": flags,
                "position": 0,
                "mode": "r+" if (flags & 0o2) else "r"
            }
            
            return fd
    
    def close(self, fd: int) -> bool:
        """Close a file descriptor"""
        with self._lock:
            if fd in self.open_files:
                del self.open_files[fd]
                return True
            return False
    
    def read(self, fd: int, size: int) -> bytes:
        """Read from file descriptor"""
        if fd not in self.open_files:
            raise ValueError(f"Invalid file descriptor: {fd}")
        
        file_info = self.open_files[fd]
        content = self.read_file(file_info["path"], file_info["position"], size)
        file_info["position"] += len(content)
        return content
    
    def write(self, fd: int, data: bytes) -> int:
        """Write to file descriptor"""
        if fd not in self.open_files:
            raise ValueError(f"Invalid file descriptor: {fd}")
        
        file_info = self.open_files[fd]
        if file_info["mode"] not in ["w", "r+", "a"]:
            raise PermissionError("File not opened for writing")
        
        self.write_file(file_info["path"], data)
        file_info["position"] = len(data)
        return len(data)
    
    def list_directory(self, path: str) -> List[Dict[str, Any]]:
        """List directory contents"""
        with self._lock:
            # Check cache
            cached = self.cache.get(f"dir:{path}")
            if cached:
                return cached
            
            inode_id, _ = self._resolve_path(path)
            
            if inode_id not in self.inodes:
                raise FileNotFoundError(f"Path not found: {path}")
            
            inode = self.inodes[inode_id]
            if inode.file_type != FileType.DIRECTORY:
                raise NotADirectoryError(f"Not a directory: {path}")
            
            if inode_id not in self.directory_entries:
                return []
            
            entries = []
            for name, entry in self.directory_entries[inode_id].items():
                child_inode = self.inodes[entry.inode_id]
                entries.append({
                    "name": name,
                    "type": entry.entry_type.value,
                    "size": child_inode.size,
                    "permissions": oct(child_inode.permissions),
                    "modified_at": datetime.fromtimestamp(child_inode.modified_at).isoformat()
                })
            
            # Sort by name
            entries.sort(key=lambda x: x["name"])
            
            # Cache result
            self.cache.put(f"dir:{path}", entries)
            
            return entries
    
    def remove(self, path: str) -> bool:
        """Remove a file or empty directory"""
        with self._lock:
            inode_id, filename = self._resolve_path(path)
            parent_path = str(PurePosixPath(path).parent)
            parent_inode, _ = self._resolve_path(parent_path if parent_path != "." else "/")
            
            if inode_id not in self.inodes:
                raise FileNotFoundError(f"File not found: {path}")
            
            inode = self.inodes[inode_id]
            
            # Free data blocks for files
            if inode.file_type == FileType.FILE:
                self.device.free_blocks(inode.block_pointers)
            
            # Remove from parent directory
            if parent_inode in self.directory_entries and filename in self.directory_entries[parent_inode]:
                del self.directory_entries[parent_inode][filename]
            
            # Remove inode
            del self.inodes[inode_id]
            if inode_id in self.directory_entries:
                del self.directory_entries[inode_id]
            
            # Journal entry
            self._journal_write(JournalEntry(
                transaction_id=len(self.journal),
                timestamp=time.time(),
                operation="DELETE",
                path=path
            ))
            
            self.cache.invalidate(parent_path)
            return True
    
    def exists(self, path: str) -> bool:
        """Check if path exists"""
        try:
            self._resolve_path(path)
            return True
        except:
            return False
    
    def _journal_write(self, entry: JournalEntry):
        """Write to journal for recovery"""
        self.journal.append(entry)
        # Keep only last 1000 entries
        if len(self.journal) > 1000:
            self.journal = self.journal[-1000:]
    
    def get_stats(self) -> Dict[str, Any]:
        """Get file system statistics"""
        with self._lock:
            total_files = sum(1 for i in self.inodes.values() if i.file_type == FileType.FILE)
            total_dirs = sum(1 for i in self.inodes.values() if i.file_type == FileType.DIRECTORY)
            
            return {
                "initialized": self.initialized,
                "total_inodes": len(self.inodes),
                "total_files": total_files,
                "total_directories": total_dirs,
                "open_files": len(self.open_files),
                "cache_size": len(self.cache.cache),
                "journal_entries": len(self.journal),
                "mount_points": list(self.mount_points.keys()),
                "device": self.device.get_stats()
            }
    
    def export_tree(self, path: str = "/") -> Dict[str, Any]:
        """Export directory tree as JSON"""
        inode_id, _ = self._resolve_path(path)
        inode = self.inodes[inode_id]
        
        result = {
            "name": PurePosixPath(path).name or "/",
            "type": inode.file_type.value,
            "size": inode.size,
            "permissions": oct(inode.permissions),
            "children": []
        }
        
        if inode.file_type == FileType.DIRECTORY and inode_id in self.directory_entries:
            for name, entry in self.directory_entries[inode_id].items():
                child_path = f"{path.rstrip('/')}/{name}"
                result["children"].append(self.export_tree(child_path))
        
        return result

# Singleton instance
_fs_instance: Optional[VirtualFileSystem] = None

def get_filesystem() -> VirtualFileSystem:
    global _fs_instance
    if _fs_instance is None:
        _fs_instance = VirtualFileSystem()
    return _fs_instance

if __name__ == "__main__":
    # Test file system
    fs = get_filesystem()
    fs.format("TEST_VOLUME")
    
    # Create test structure
    fs.mkdir("/home")
    fs.mkdir("/home/user")
    fs.create_file("/home/user/test.txt", content=b"Hello FalconOS!")
    
    print("\nDirectory listing:")
    for entry in fs.list_directory("/home"):
        print(f"  {entry['name']} ({entry['type']})")
    
    print("\nFile content:", fs.read_file("/home/user/test.txt"))
    print("\nFS Stats:", json.dumps(fs.get_stats(), indent=2))
