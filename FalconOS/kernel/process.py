#!/usr/bin/env python3
"""
FalconOS v2.1 - Process Manager ve Çoklu Görev Sistemi
Gerçek zamanlı process yönetimi, thread pool ve görev zamanlayıcı
"""

import os
import sys
import time
import threading
import multiprocessing
import queue
import signal
import hashlib
from datetime import datetime
from typing import Dict, List, Optional, Any, Callable, Tuple
from dataclasses import dataclass, field
from enum import Enum
import json
import pickle
import subprocess
import ctypes

from kernel.core import SystemConfig, Logger, FalconError, ErrorCode

logger = Logger()

# ============================================================================
# PROCESS DURUMLARI VE TİPLERİ
# ============================================================================

class ProcessState(Enum):
    """Process durumları"""
    NEW = "new"
    READY = "ready"
    RUNNING = "running"
    WAITING = "waiting"
    STOPPED = "stopped"
    ZOMBIE = "zombie"
    TERMINATED = "terminated"

class ProcessPriority(Enum):
    """Process öncelikleri"""
    REALTIME = 0      # En yüksek öncelik
    HIGH = 1
    NORMAL = 2
    LOW = 3
    IDLE = 4          # En düşük öncelik

class ProcessType(Enum):
    """Process tipleri"""
    SYSTEM = "system"     # Sistem process'i
    USER = "user"         # Kullanıcı uygulaması
    DAEMON = "daemon"     # Arka plan servisi
    KERNEL = "kernel"     # Kernel thread'i

@dataclass
class ProcessControlBlock:
    """Process Control Block - Her process için metadata"""
    pid: int
    ppid: int  # Parent PID
    name: str
    process_type: ProcessType
    state: ProcessState = ProcessState.NEW
    priority: ProcessPriority = ProcessPriority.NORMAL
    created_at: datetime = field(default_factory=datetime.now)
    started_at: Optional[datetime] = None
    ended_at: Optional[datetime] = None
    
    # Bellek bilgisi
    memory_pages: List[int] = field(default_factory=list)
    stack_pointer: int = 0
    program_counter: int = 0
    
    # CPU durumu
    cpu_time: float = 0.0
    user_time: float = 0.0
    system_time: float = 0.0
    
    # Dosya descriptor'ları
    open_files: Dict[int, str] = field(default_factory=dict)  # fd -> path
    
    # Thread bilgisi
    threads: List[int] = field(default_factory=list)  # Thread ID'leri
    
    # Environment
    environment: Dict[str, str] = field(default_factory=dict)
    working_directory: str = "/"
    
    # İstatistikler
    context_switches: int = 0
    page_faults: int = 0
    io_operations: int = 0
    
    # Exit bilgisi
    exit_code: Optional[int] = None
    exit_signal: Optional[int] = None
    
    def to_dict(self) -> Dict[str, Any]:
        """Dictionary'e çevir"""
        return {
            'pid': self.pid,
            'ppid': self.ppid,
            'name': self.name,
            'type': self.process_type.value,
            'state': self.state.value,
            'priority': self.priority.value,
            'created': self.created_at.isoformat(),
            'started': self.started_at.isoformat() if self.started_at else None,
            'ended': self.ended_at.isoformat() if self.ended_at else None,
            'memory_pages': len(self.memory_pages),
            'cpu_time': self.cpu_time,
            'exit_code': self.exit_code,
            'threads': len(self.threads),
            'working_directory': self.working_directory
        }

# ============================================================================
# THREAD YÖNETİMİ
# ============================================================================

@dataclass
class ThreadControlBlock:
    """Thread Control Block"""
    tid: int
    pid: int  # Sahip process
    name: str
    state: ProcessState = ProcessState.NEW
    priority: ProcessPriority = ProcessPriority.NORMAL
    created_at: datetime = field(default_factory=datetime.now)
    cpu_time: float = 0.0
    stack_base: int = 0
    registers: Dict[str, Any] = field(default_factory=dict)

class ThreadManager:
    """Thread yöneticisi"""
    def __init__(self):
        self.threads: Dict[int, ThreadControlBlock] = {}
        self.tid_counter = 0
        self.lock = threading.Lock()
        self.thread_pool: List[threading.Thread] = []
        self.task_queue = queue.Queue()
        
        # Worker thread'leri başlat
        self._start_worker_threads()
        logger.info("Thread yöneticisi başlatıldı")
    
    def _start_worker_threads(self, count: int = 4):
        """Worker thread havuzu oluştur"""
        for i in range(count):
            worker = threading.Thread(target=self._worker_loop, daemon=True)
            worker.start()
            self.thread_pool.append(worker)
        
        logger.debug(f"{count} worker thread başlatıldı")
    
    def _worker_loop(self):
        """Worker thread döngüsü"""
        while True:
            try:
                task = self.task_queue.get(timeout=1)
                if task is None:
                    break
                
                func, args, kwargs = task
                try:
                    func(*args, **kwargs)
                except Exception as e:
                    logger.error(f"Thread görev hatası: {e}")
                
                self.task_queue.task_done()
            except queue.Empty:
                continue
    
    def create_thread(self, pid: int, target: Callable, 
                     args: tuple = (), kwargs: dict = None,
                     name: str = "", priority: ProcessPriority = ProcessPriority.NORMAL) -> int:
        """Yeni thread oluştur"""
        with self.lock:
            self.tid_counter += 1
            tid = self.tid_counter
            
            tcb = ThreadControlBlock(
                tid=tid,
                pid=pid,
                name=name or f"thread-{tid}",
                state=ProcessState.READY,
                priority=priority
            )
            
            self.threads[tid] = tcb
            
            # Gerçek Python thread'i başlat
            thread = threading.Thread(
                target=self._run_thread,
                args=(tid, target, args, kwargs or {}),
                name=tcb.name,
                daemon=True
            )
            thread.start()
            
            logger.debug(f"Thread oluşturuldu: TID={tid}, PID={pid}, name={tcb.name}")
            return tid
    
    def _run_thread(self, tid: int, target: Callable, args: tuple, kwargs: dict):
        """Thread'i çalıştır"""
        if tid not in self.threads:
            return
        
        tcb = self.threads[tid]
        tcb.state = ProcessState.RUNNING
        start_time = time.time()
        
        try:
            target(*args, **kwargs)
            tcb.state = ProcessState.TERMINATED
        except Exception as e:
            logger.error(f"Thread {tid} hatası: {e}")
            tcb.state = ProcessState.TERMINATED
            tcb.exit_code = -1
        finally:
            end_time = time.time()
            tcb.cpu_time = end_time - start_time
            tcb.ended_at = datetime.now()
    
    def get_thread(self, tid: int) -> Optional[ThreadControlBlock]:
        """Thread bilgilerini al"""
        return self.threads.get(tid)
    
    def list_threads(self, pid: Optional[int] = None) -> List[Dict]:
        """Thread'leri listele"""
        with self.lock:
            threads = []
            for tid, tcb in self.threads.items():
                if pid is None or tcb.pid == pid:
                    threads.append({
                        'tid': tcb.tid,
                        'pid': tcb.pid,
                        'name': tcb.name,
                        'state': tcb.state.value,
                        'cpu_time': tcb.cpu_time,
                        'created': tcb.created_at.isoformat()
                    })
            return threads
    
    def terminate_thread(self, tid: int) -> bool:
        """Thread'i sonlandır"""
        with self.lock:
            if tid in self.threads:
                self.threads[tid].state = ProcessState.TERMINATED
                logger.debug(f"Thread sonlandırıldı: TID={tid}")
                return True
            return False

# ============================================================================
# GÖREV ZAMANLAYICI (SCHEDULER)
# ============================================================================

class SchedulerPolicy(Enum):
    """Zamanlayıcı politikaları"""
    FIFO = "fifo"           # First In First Out
    ROUND_ROBIN = "rr"      # Round Robin
    PRIORITY = "priority"   # Öncelikli
    CFS = "cfs"             # Completely Fair Scheduler

@dataclass
class SchedulerConfig:
    """Zamanlayıcı yapılandırması"""
    policy: SchedulerPolicy = SchedulerPolicy.ROUND_ROBIN
    time_slice_ms: int = 100  # Time slice (Round Robin için)
    min_priority: int = 0
    max_priority: int = 4
    nice_values: Dict[int, int] = field(default_factory=dict)  # PID -> nice value

class Scheduler:
    """Process zamanlayıcı"""
    def __init__(self, config: SchedulerConfig = None):
        self.config = config or SchedulerConfig()
        self.ready_queue: List[ProcessControlBlock] = []
        self.waiting_queue: Dict[int, ProcessControlBlock] = {}  # event_id -> PCB
        self.running_process: Optional[ProcessControlBlock] = None
        self.lock = threading.Lock()
        self.scheduler_thread: Optional[threading.Thread] = None
        self.running = False
        
        # İstatistikler
        self.context_switches = 0
        self.total_wait_time = 0.0
        self.total_turnaround_time = 0.0
        
        logger.info("Zamanlayıcı başlatıldı")
    
    def start(self):
        """Zamanlayıcıyı başlat"""
        self.running = True
        self.scheduler_thread = threading.Thread(target=self._scheduler_loop, daemon=True)
        self.scheduler_thread.start()
        logger.info("Zamanlayıcı döngüsü başlatıldı")
    
    def stop(self):
        """Zamanlayıcıyı durdur"""
        self.running = False
        if self.scheduler_thread:
            self.scheduler_thread.join(timeout=2)
    
    def _scheduler_loop(self):
        """Zamanlayıcı ana döngüsü"""
        while self.running:
            try:
                self._schedule()
                time.sleep(self.config.time_slice_ms / 1000.0)
            except Exception as e:
                logger.error(f"Zamanlayıcı hatası: {e}")
    
    def _schedule(self):
        """Sonraki process'i seç"""
        with self.lock:
            if not self.ready_queue:
                return
            
            # Politikaya göre seçim yap
            if self.config.policy == SchedulerPolicy.FIFO:
                next_process = self.ready_queue.pop(0)
            elif self.config.policy == SchedulerPolicy.ROUND_ROBIN:
                # Mevcut process'i kuyruğun sonuna ekle
                if self.running_process and self.running_process.state == ProcessState.RUNNING:
                    self.running_process.state = ProcessState.READY
                    self.ready_queue.append(self.running_process)
                next_process = self.ready_queue.pop(0) if self.ready_queue else None
            elif self.config.policy == SchedulerPolicy.PRIORITY:
                # En yüksek öncelikli process'i seç
                self.ready_queue.sort(key=lambda p: p.priority.value)
                next_process = self.ready_queue.pop(0)
            else:  # CFS
                # En az CPU zamanı alan process'i seç
                self.ready_queue.sort(key=lambda p: p.cpu_time)
                next_process = self.ready_queue.pop(0)
            
            if next_process:
                self._context_switch(next_process)
    
    def _context_switch(self, next_process: ProcessControlBlock):
        """Context switch gerçekleştir"""
        if self.running_process:
            # Eski process'i kaydet
            old = self.running_process
            old.context_switches += 1
            self.context_switches += 1
            
            if old.state == ProcessState.RUNNING:
                old.state = ProcessState.READY
                if self.config.policy != SchedulerPolicy.FIFO:
                    self.ready_queue.append(old)
        
        # Yeni process'i çalıştır
        self.running_process = next_process
        next_process.state = ProcessState.RUNNING
        if not next_process.started_at:
            next_process.started_at = datetime.now()
        
        logger.debug(f"Context switch: {next_process.pid} ({next_process.name})")
    
    def add_to_ready(self, pcb: ProcessControlBlock):
        """Process'i ready kuyruğuna ekle"""
        with self.lock:
            pcb.state = ProcessState.READY
            if pcb not in self.ready_queue:
                self.ready_queue.append(pcb)
    
    def add_to_waiting(self, pcb: ProcessControlBlock, event_id: int):
        """Process'i waiting kuyruğuna ekle"""
        with self.lock:
            pcb.state = ProcessState.WAITING
            self.waiting_queue[event_id] = pcb
            if pcb in self.ready_queue:
                self.ready_queue.remove(pcb)
    
    def wake_up(self, event_id: int):
        """Process'i uyandır"""
        with self.lock:
            if event_id in self.waiting_queue:
                pcb = self.waiting_queue.pop(event_id)
                self.add_to_ready(pcb)
    
    def get_stats(self) -> Dict[str, Any]:
        """Zamanlayıcı istatistikleri"""
        return {
            'policy': self.config.policy.value,
            'ready_queue_size': len(self.ready_queue),
            'waiting_queue_size': len(self.waiting_queue),
            'running_process': self.running_process.pid if self.running_process else None,
            'context_switches': self.context_switches,
            'time_slice_ms': self.config.time_slice_ms
        }

# ============================================================================
# PROCESS YÖNETİCİSİ
# ============================================================================

class ProcessManager:
    """Ana process yöneticisi"""
    def __init__(self, memory_manager=None):
        self.processes: Dict[int, ProcessControlBlock] = {}
        self.pid_counter = 0
        self.max_pid = SystemConfig.MAX_PROCESSES
        self.lock = threading.Lock()
        
        self.memory_manager = memory_manager
        self.thread_manager = ThreadManager()
        self.scheduler = Scheduler()
        
        # Init process'i oluştur (PID 1)
        self._create_init_process()
        
        logger.info("Process yöneticisi başlatıldı")
    
    def _create_init_process(self):
        """Init process'ini oluştur (PID 1)"""
        init_pcb = ProcessControlBlock(
            pid=1,
            ppid=0,
            name="init",
            process_type=ProcessType.SYSTEM,
            state=ProcessState.RUNNING,
            priority=ProcessPriority.HIGH,
            started_at=datetime.now()
        )
        
        self.processes[1] = init_pcb
        self.pid_counter = 1
        self.scheduler.running_process = init_pcb
        
        logger.info("Init process başlatıldı (PID=1)")
    
    def allocate_pid(self) -> int:
        """Yeni PID tahsis et"""
        with self.lock:
            if self.pid_counter >= self.max_pid:
                raise FalconError("Maksimum process sayısına ulaşıldı", 
                                 ErrorCode.PROCESS_LIMIT.value)
            
            self.pid_counter += 1
            return self.pid_counter
    
    def create_process(self, name: str, target: Callable = None,
                      args: tuple = (), kwargs: dict = None,
                      process_type: ProcessType = ProcessType.USER,
                      priority: ProcessPriority = ProcessPriority.NORMAL,
                      parent_pid: int = 1,
                      environment: Dict[str, str] = None) -> int:
        """Yeni process oluştur"""
        with self.lock:
            pid = self.allocate_pid()
            
            pcb = ProcessControlBlock(
                pid=pid,
                ppid=parent_pid,
                name=name,
                process_type=process_type,
                priority=priority,
                environment=environment or {},
                working_directory="/"
            )
            
            # Bellek ayır
            if self.memory_manager:
                try:
                    pages = self.memory_manager.allocate_pages(pid, 4)  # 4 sayfa
                    pcb.memory_pages = pages
                except FalconError as e:
                    logger.warning(f"Process {pid} için bellek ayrılamadı: {e}")
            
            self.processes[pid] = pcb
            
            # Process'i çalıştır
            if target:
                thread = threading.Thread(
                    target=self._run_process,
                    args=(pid, target, args, kwargs or {}),
                    daemon=True
                )
                thread.start()
                pcb.threads.append(self.thread_manager.tid_counter + 1)
            
            # Ready kuyruğuna ekle
            self.scheduler.add_to_ready(pcb)
            
            logger.info(f"Process oluşturuldu: PID={pid}, name={name}, type={process_type.value}")
            return pid
    
    def _run_process(self, pid: int, target: Callable, args: tuple, kwargs: dict):
        """Process'i çalıştır"""
        if pid not in self.processes:
            return
        
        pcb = self.processes[pid]
        pcb.state = ProcessState.RUNNING
        pcb.started_at = datetime.now()
        start_time = time.time()
        
        try:
            target(*args, **kwargs)
            pcb.exit_code = 0
            pcb.state = ProcessState.TERMINATED
        except Exception as e:
            logger.error(f"Process {pid} hatası: {e}")
            pcb.exit_code = -1
            pcb.state = ProcessState.TERMINATED
        finally:
            end_time = time.time()
            pcb.cpu_time = end_time - start_time
            pcb.ended_at = datetime.now()
            
            # Belleği serbest bırak
            if self.memory_manager:
                self.memory_manager.free_pages(pid)
            
            logger.debug(f"Process sonlandı: PID={pid}, cpu_time={pcb.cpu_time:.3f}s")
    
    def fork(self, parent_pid: int) -> int:
        """Process çoğalt (fork)"""
        if parent_pid not in self.processes:
            raise FalconError("Parent process bulunamadı", ErrorCode.FILE_NOT_FOUND.value)
        
        parent = self.processes[parent_pid]
        
        with self.lock:
            child_pid = self.allocate_pid()
            
            # Parent'ın kopyasını oluştur
            child = ProcessControlBlock(
                pid=child_pid,
                ppid=parent_pid,
                name=parent.name,
                process_type=parent.process_type,
                priority=parent.priority,
                environment=parent.environment.copy(),
                working_directory=parent.working_directory
            )
            
            # Bellek kopyala (Copy-on-Write simülasyonu)
            if self.memory_manager and parent.memory_pages:
                try:
                    child_pages = self.memory_manager.allocate_pages(child_pid, 
                                                                   len(parent.memory_pages))
                    child.memory_pages = child_pages
                    
                    # Veriyi kopyala
                    for i, page_id in enumerate(parent.memory_pages):
                        if i < len(child_pages):
                            data = self.memory_manager.read_page(page_id)
                            self.memory_manager.write_page(child_pages[i], data)
                except FalconError as e:
                    logger.warning(f"Fork bellek hatası: {e}")
            
            self.processes[child_pid] = child
            self.scheduler.add_to_ready(child)
            
            logger.info(f"Fork: Parent={parent_pid}, Child={child_pid}")
            return child_pid
    
    def wait(self, pid: int, timeout: float = None) -> Tuple[int, int]:
        """Process sonlanmasını bekle"""
        start_time = time.time()
        
        while True:
            if pid not in self.processes:
                return pid, -1
            
            pcb = self.processes[pid]
            
            if pcb.state in [ProcessState.TERMINATED, ProcessState.ZOMBIE]:
                exit_code = pcb.exit_code or 0
                # Zombie process'i temizle
                del self.processes[pid]
                return pid, exit_code
            
            if timeout and (time.time() - start_time) > timeout:
                raise FalconError("Bekleme süresi aşıldı", ErrorCode.TIMEOUT.value)
            
            time.sleep(0.1)
    
    def kill(self, pid: int, signal: int = 9) -> bool:
        """Process'i sonlandır"""
        if pid not in self.processes:
            return False
        
        if pid == 1:  # Init process'i öldürme
            logger.warning("Init process'i öldürme girişimi engellendi")
            return False
        
        pcb = self.processes[pid]
        pcb.exit_signal = signal
        pcb.exit_code = -signal
        pcb.state = ProcessState.TERMINATED
        pcb.ended_at = datetime.now()
        
        # Belleği serbest bırak
        if self.memory_manager:
            self.memory_manager.free_pages(pid)
        
        logger.info(f"Process sonlandırıldı: PID={pid}, signal={signal}")
        return True
    
    def send_signal(self, pid: int, signal: int) -> bool:
        """Process'e sinyal gönder"""
        if pid not in self.processes:
            return False
        
        # Sinyal işleme mantığı (basitleştirilmiş)
        pcb = self.processes[pid]
        
        if signal == signal.SIGSTOP:
            pcb.state = ProcessState.STOPPED
        elif signal == signal.SIGCONT:
            if pcb.state == ProcessState.STOPPED:
                pcb.state = ProcessState.READY
                self.scheduler.add_to_ready(pcb)
        elif signal == signal.SIGTERM:
            return self.kill(pid, signal.SIGTERM)
        elif signal == signal.SIGKILL:
            return self.kill(pid, signal.SIGKILL)
        
        logger.debug(f"Sinyal gönderildi: PID={pid}, signal={signal}")
        return True
    
    def get_process(self, pid: int) -> Optional[ProcessControlBlock]:
        """Process bilgilerini al"""
        return self.processes.get(pid)
    
    def list_processes(self) -> List[Dict]:
        """Tüm process'leri listele"""
        with self.lock:
            return [pcb.to_dict() for pcb in self.processes.values()]
    
    def get_process_tree(self) -> Dict[int, List[int]]:
        """Process ağacı oluştur"""
        tree = {}
        
        for pid, pcb in self.processes.items():
            if pcb.ppid not in tree:
                tree[pcb.ppid] = []
            tree[pcb.ppid].append(pid)
        
        return tree
    
    def get_stats(self) -> Dict[str, Any]:
        """Process yöneticisi istatistikleri"""
        with self.lock:
            states = {}
            types = {}
            
            for pcb in self.processes.values():
                state_name = pcb.state.value
                type_name = pcb.process_type.value
                
                states[state_name] = states.get(state_name, 0) + 1
                types[type_name] = types.get(type_name, 0) + 1
            
            total_cpu = sum(pcb.cpu_time for pcb in self.processes.values())
            
            return {
                'total_processes': len(self.processes),
                'max_processes': self.max_pid,
                'states': states,
                'types': types,
                'total_cpu_time': total_cpu,
                'scheduler': self.scheduler.get_stats(),
                'threads': len(self.thread_manager.threads)
            }

# Devam edecek... (Device Drivers, System Calls, IPC vb.)
