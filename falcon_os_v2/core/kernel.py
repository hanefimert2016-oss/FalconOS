#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FalconOS v2.0 Alpha - Core Kernel Module
High-performance microkernel with preemptive multitasking simulation
"""

import time
import threading
import queue
import json
import os
import sys
from datetime import datetime
from typing import Dict, List, Optional, Any, Callable
from dataclasses import dataclass, field
from enum import Enum
import hashlib
import pickle

class ProcessState(Enum):
    READY = "ready"
    RUNNING = "running"
    BLOCKED = "blocked"
    TERMINATED = "terminated"

class Priority(Enum):
    CRITICAL = 0
    HIGH = 1
    NORMAL = 2
    LOW = 3
    IDLE = 4

@dataclass
class Process:
    pid: int
    name: str
    priority: Priority
    state: ProcessState = ProcessState.READY
    created_at: float = field(default_factory=time.time)
    cpu_time: float = 0.0
    memory_allocated: int = 0
    parent_pid: Optional[int] = None
    threads: List[threading.Thread] = field(default_factory=list)
    resources: Dict[str, Any] = field(default_factory=dict)

@dataclass
class MemoryBlock:
    start_addr: int
    size: int
    allocated: bool = False
    process_id: Optional[int] = None
    data: bytes = b""

class VirtualMemoryManager:
    """Advanced virtual memory management with paging simulation"""
    
    def __init__(self, total_memory: int = 1024 * 1024 * 512):  # 512MB simulated
        self.total_memory = total_memory
        self.page_size = 4096  # 4KB pages
        self.pages = total_memory // self.page_size
        self.memory_map: Dict[int, MemoryBlock] = {}
        self.free_list: List[int] = list(range(self.pages))
        self.process_pages: Dict[int, List[int]] = {}  # pid -> [page_ids]
        self._lock = threading.Lock()
        
    def allocate_pages(self, pid: int, num_pages: int) -> List[int]:
        with self._lock:
            if len(self.free_list) < num_pages:
                raise MemoryError(f"Insufficient memory. Requested: {num_pages}, Available: {len(self.free_list)}")
            
            allocated_pages = self.free_list[:num_pages]
            self.free_list = self.free_list[num_pages:]
            
            for page_id in allocated_pages:
                self.memory_map[page_id] = MemoryBlock(
                    start_addr=page_id * self.page_size,
                    size=self.page_size,
                    allocated=True,
                    process_id=pid
                )
            
            if pid not in self.process_pages:
                self.process_pages[pid] = []
            self.process_pages[pid].extend(allocated_pages)
            
            return allocated_pages
    
    def free_pages(self, pid: int):
        with self._lock:
            if pid in self.process_pages:
                for page_id in self.process_pages[pid]:
                    if page_id in self.memory_map:
                        del self.memory_map[page_id]
                    self.free_list.append(page_id)
                del self.process_pages[pid]
    
    def write_page(self, page_id: int, data: bytes):
        if page_id in self.memory_map:
            self.memory_map[page_id].data = data[:self.page_size]
    
    def read_page(self, page_id: int) -> bytes:
        if page_id in self.memory_map:
            return self.memory_map[page_id].data
        return b""
    
    def get_memory_stats(self) -> Dict[str, Any]:
        with self._lock:
            used_pages = len(self.memory_map)
            return {
                "total_memory_mb": self.total_memory / (1024 * 1024),
                "used_memory_mb": (used_pages * self.page_size) / (1024 * 1024),
                "free_memory_mb": (len(self.free_list) * self.page_size) / (1024 * 1024),
                "page_count": self.pages,
                "used_pages": used_pages,
                "free_pages": len(self.free_list)
            }

class Scheduler:
    """Multi-level feedback queue scheduler"""
    
    def __init__(self):
        self.queues: Dict[Priority, queue.PriorityQueue] = {
            priority: queue.PriorityQueue() for priority in Priority
        }
        self.current_process: Optional[Process] = None
        self.process_table: Dict[int, Process] = {}
        self.next_pid = 1
        self._lock = threading.Lock()
        self.time_quantum = 0.01  # 10ms time slice
        
    def create_process(self, name: str, priority: Priority = Priority.NORMAL, 
                      parent_pid: Optional[int] = None) -> Process:
        with self._lock:
            pid = self.next_pid
            self.next_pid += 1
            
            process = Process(
                pid=pid,
                name=name,
                priority=priority,
                parent_pid=parent_pid
            )
            
            self.process_table[pid] = process
            self.queues[priority].put((priority.value, time.time(), pid))
            
            return process
    
    def schedule_next(self) -> Optional[Process]:
        for priority in Priority:
            if not self.queues[priority].empty():
                _, _, pid = self.queues[priority].get()
                if pid in self.process_table:
                    process = self.process_table[pid]
                    if process.state != ProcessState.TERMINATED:
                        self.current_process = process
                        process.state = ProcessState.RUNNING
                        return process
        return None
    
    def yield_process(self, pid: int):
        if pid in self.process_table:
            process = self.process_table[pid]
            process.state = ProcessState.READY
            self.queues[process.priority].put((process.priority.value, time.time(), pid))
            self.current_process = None
    
    def terminate_process(self, pid: int):
        if pid in self.process_table:
            self.process_table[pid].state = ProcessState.TERMINATED
            if self.current_process and self.current_process.pid == pid:
                self.current_process = None
    
    def get_process_list(self) -> List[Dict[str, Any]]:
        return [
            {
                "pid": p.pid,
                "name": p.name,
                "state": p.state.value,
                "priority": p.priority.name,
                "cpu_time": p.cpu_time,
                "memory_mb": p.memory_allocated / (1024 * 1024)
            }
            for p in self.process_table.values()
        ]

class SystemCallHandler:
    """Handle system calls from user space"""
    
    def __init__(self, kernel: 'Kernel'):
        self.kernel = kernel
        
    def handle_syscall(self, syscall_num: int, args: tuple) -> Any:
        syscalls = {
            0: self._sys_read,
            1: self._sys_write,
            2: self._sys_open,
            3: self._sys_close,
            4: self._sys_fork,
            5: self._sys_exec,
            6: self._sys_exit,
            7: self._sys_wait,
            8: self._sys_getpid,
            9: self._sys_mmap,
            10: self._sys_munmap,
        }
        
        if syscall_num in syscalls:
            return syscalls[syscall_num](*args)
        raise ValueError(f"Unknown syscall: {syscall_num}")
    
    def _sys_read(self, fd: int, size: int) -> bytes:
        return self.kernel.filesystem.read(fd, size)
    
    def _sys_write(self, fd: int, data: bytes) -> int:
        return self.kernel.filesystem.write(fd, data)
    
    def _sys_open(self, path: str, flags: int) -> int:
        return self.kernel.filesystem.open(path, flags)
    
    def _sys_close(self, fd: int) -> bool:
        return self.kernel.filesystem.close(fd)
    
    def _sys_fork(self) -> int:
        # Simplified fork simulation
        current = self.kernel.scheduler.current_process
        if current:
            new_process = self.kernel.scheduler.create_process(
                f"{current.name}_child",
                current.priority,
                current.pid
            )
            return new_process.pid
        return -1
    
    def _sys_exec(self, path: str, args: List[str]) -> int:
        return self.kernel.execute_program(path, args)
    
    def _sys_exit(self, status: int):
        if self.kernel.scheduler.current_process:
            self.kernel.scheduler.terminate_process(
                self.kernel.scheduler.current_process.pid
            )
    
    def _sys_wait(self, pid: int) -> int:
        # Simplified wait implementation
        return 0
    
    def _sys_getpid(self) -> int:
        if self.kernel.scheduler.current_process:
            return self.kernel.scheduler.current_process.pid
        return -1
    
    def _sys_mmap(self, size: int) -> int:
        if self.kernel.scheduler.current_process:
            pages = self.kernel.memory_manager.allocate_pages(
                self.kernel.scheduler.current_process.pid,
                (size + 4095) // 4096
            )
            return pages[0] * 4096 if pages else -1
        return -1
    
    def _sys_munmap(self, addr: int, size: int) -> bool:
        if self.kernel.scheduler.current_process:
            self.kernel.memory_manager.free_pages(
                self.kernel.scheduler.current_process.pid
            )
            return True
        return False

class Kernel:
    """Main FalconOS Kernel"""
    
    VERSION = "2.0.0-alpha"
    BUILD_DATE = datetime.now().isoformat()
    
    def __init__(self):
        self.memory_manager = VirtualMemoryManager()
        self.scheduler = Scheduler()
        self.syscall_handler = SystemCallHandler(self)
        self.filesystem = None  # Will be initialized by FileSystem module
        self.running = False
        self.boot_time = None
        self.services: Dict[str, Any] = {}
        self.interrupt_handlers: Dict[int, Callable] = {}
        
    def initialize(self, filesystem):
        """Initialize kernel subsystems"""
        self.filesystem = filesystem
        self.boot_time = time.time()
        self.running = True
        
        # Create idle process
        idle_process = self.scheduler.create_process("idle", Priority.IDLE)
        idle_process.state = ProcessState.RUNNING
        self.scheduler.current_process = idle_process
        
        # Register default interrupt handlers
        self.register_interrupt(0, self._timer_interrupt)
        self.register_interrupt(1, self._keyboard_interrupt)
        
        print(f"[KERNEL] FalconOS v{self.VERSION} initialized successfully")
        print(f"[KERNEL] Boot time: {datetime.fromtimestamp(self.boot_time)}")
        print(f"[KERNEL] Memory: {self.memory_manager.get_memory_stats()['total_memory_mb']:.0f}MB")
        
    def register_interrupt(self, irq: int, handler: Callable):
        self.interrupt_handlers[irq] = handler
        
    def trigger_interrupt(self, irq: int, data: Any = None):
        if irq in self.interrupt_handlers:
            return self.interrupt_handlers[irq](data)
        return False
    
    def _timer_interrupt(self, data: Any):
        """Handle timer interrupt for process scheduling"""
        if self.scheduler.current_process:
            self.scheduler.current_process.cpu_time += self.scheduler.time_quantum
            self.scheduler.yield_process(self.scheduler.current_process.pid)
        next_process = self.scheduler.schedule_next()
        return next_process is not None
    
    def _keyboard_interrupt(self, data: Any):
        """Handle keyboard input"""
        return True
    
    def execute_program(self, path: str, args: List[str]) -> int:
        """Execute a program in a new process"""
        if not self.filesystem.exists(path):
            raise FileNotFoundError(f"Program not found: {path}")
        
        process = self.scheduler.create_process(
            os.path.basename(path),
            Priority.NORMAL
        )
        
        # Allocate memory for the process
        try:
            pages = self.memory_manager.allocate_pages(process.pid, 10)  # 40KB default
            process.memory_allocated = len(pages) * 4096
        except MemoryError as e:
            self.scheduler.terminate_process(process.pid)
            raise e
        
        # In a real OS, we would load the binary here
        # For simulation, we just mark it as ready
        print(f"[KERNEL] Executing: {path} (PID: {process.pid})")
        return process.pid
    
    def get_system_info(self) -> Dict[str, Any]:
        uptime = time.time() - self.boot_time if self.boot_time else 0
        return {
            "version": self.VERSION,
            "build_date": self.BUILD_DATE,
            "uptime_seconds": uptime,
            "uptime_formatted": self._format_uptime(uptime),
            "processes": len([p for p in self.scheduler.process_table.values() 
                            if p.state != ProcessState.TERMINATED]),
            "memory": self.memory_manager.get_memory_stats(),
            "services": list(self.services.keys())
        }
    
    @staticmethod
    def _format_uptime(seconds: float) -> str:
        days = int(seconds // 86400)
        hours = int((seconds % 86400) // 3600)
        minutes = int((seconds % 3600) // 60)
        secs = int(seconds % 60)
        return f"{days}d {hours:02d}:{minutes:02d}:{secs:02d}"
    
    def shutdown(self):
        """Graceful system shutdown"""
        print("[KERNEL] Initiating graceful shutdown...")
        self.running = False
        
        # Terminate all user processes
        for pid, process in list(self.scheduler.process_table.items()):
            if process.name != "idle":
                self.scheduler.terminate_process(pid)
        
        # Free all memory
        for pid in list(self.memory_manager.process_pages.keys()):
            self.memory_manager.free_pages(pid)
        
        print("[KERNEL] System halted.")

# Singleton instance
_kernel_instance: Optional[Kernel] = None

def get_kernel() -> Kernel:
    global _kernel_instance
    if _kernel_instance is None:
        _kernel_instance = Kernel()
    return _kernel_instance

if __name__ == "__main__":
    # Test kernel initialization
    kernel = get_kernel()
    print("FalconOS Kernel Module loaded successfully")
    print(f"Version: {kernel.VERSION}")
