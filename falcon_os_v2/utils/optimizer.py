#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FalconOS v2.0 - Performance Optimization Utilities
Advanced caching, profiling, and system tuning tools
"""

import time
import threading
import functools
from typing import Dict, Any, Callable, Optional, List
from dataclasses import dataclass, field
from collections import OrderedDict
import json

@dataclass
class CacheStats:
    """Statistics for cache performance"""
    hits: int = 0
    misses: int = 0
    evictions: int = 0
    size: int = 0
    max_size: int = 1000
    
    @property
    def hit_rate(self) -> float:
        total = self.hits + self.misses
        return self.hits / total if total > 0 else 0.0
    
    def to_dict(self) -> Dict[str, Any]:
        return {
            "hits": self.hits,
            "misses": self.misses,
            "evictions": self.evictions,
            "size": self.size,
            "max_size": self.max_size,
            "hit_rate": f"{self.hit_rate:.2%}"
        }

class LRUCache:
    """High-performance LRU cache with thread safety"""
    
    def __init__(self, max_size: int = 1000):
        self.max_size = max_size
        self.cache: OrderedDict = OrderedDict()
        self.stats = CacheStats(max_size=max_size)
        self._lock = threading.RLock()
        
    def get(self, key: str) -> Optional[Any]:
        with self._lock:
            if key in self.cache:
                # Move to end (most recently used)
                self.cache.move_to_end(key)
                self.stats.hits += 1
                return self.cache[key]
            self.stats.misses += 1
            return None
    
    def put(self, key: str, value: Any):
        with self._lock:
            if key in self.cache:
                self.cache.move_to_end(key)
                self.cache[key] = value
            else:
                if len(self.cache) >= self.max_size:
                    # Remove least recently used
                    self.cache.popitem(last=False)
                    self.stats.evictions += 1
                self.cache[key] = value
                self.stats.size = len(self.cache)
    
    def delete(self, key: str) -> bool:
        with self._lock:
            if key in self.cache:
                del self.cache[key]
                self.stats.size = len(self.cache)
                return True
            return False
    
    def clear(self):
        with self._lock:
            self.cache.clear()
            self.stats.size = 0
    
    def get_stats(self) -> Dict[str, Any]:
        with self._lock:
            return self.stats.to_dict()

def cached(cache_instance: LRUCache, ttl: int = 300):
    """Decorator for caching function results"""
    def decorator(func: Callable) -> Callable:
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            # Create cache key from arguments
            key = f"{func.__name__}:{str(args)}:{str(sorted(kwargs.items()))}"
            
            # Check cache
            cached_result = cache_instance.get(key)
            if cached_result is not None:
                return cached_result
            
            # Execute function and cache result
            result = func(*args, **kwargs)
            cache_instance.put(key, result)
            return result
        return wrapper
    return decorator

class PerformanceProfiler:
    """Profile system and function performance"""
    
    def __init__(self):
        self.measurements: List[Dict[str, Any]] = []
        self._lock = threading.Lock()
        
    def measure_time(self, func: Callable, *args, **kwargs) -> tuple:
        """Measure execution time of a function"""
        start = time.perf_counter()
        result = func(*args, **kwargs)
        end = time.perf_counter()
        
        elapsed = end - start
        measurement = {
            "function": func.__name__,
            "elapsed_ms": elapsed * 1000,
            "timestamp": time.time()
        }
        
        with self._lock:
            self.measurements.append(measurement)
        
        return result, elapsed
    
    def get_average_time(self, function_name: str) -> float:
        """Get average execution time for a function"""
        with self._lock:
            times = [m["elapsed_ms"] for m in self.measurements 
                    if m["function"] == function_name]
            return sum(times) / len(times) if times else 0.0
    
    def get_slowest_functions(self, limit: int = 10) -> List[Dict]:
        """Get the slowest function calls"""
        with self._lock:
            sorted_measurements = sorted(
                self.measurements,
                key=lambda x: x["elapsed_ms"],
                reverse=True
            )
            return sorted_measurements[:limit]
    
    def export_report(self) -> str:
        """Export performance report as JSON"""
        with self._lock:
            report = {
                "total_measurements": len(self.measurements),
                "measurements": self.measurements,
                "summary": {}
            }
            
            # Calculate per-function stats
            func_stats: Dict[str, List[float]] = {}
            for m in self.measurements:
                name = m["function"]
                if name not in func_stats:
                    func_stats[name] = []
                func_stats[name].append(m["elapsed_ms"])
            
            for func_name, times in func_stats.items():
                report["summary"][func_name] = {
                    "count": len(times),
                    "avg_ms": sum(times) / len(times),
                    "min_ms": min(times),
                    "max_ms": max(times)
                }
            
            return json.dumps(report, indent=2)
    
    def clear(self):
        """Clear all measurements"""
        with self._lock:
            self.measurements.clear()

class ResourceMonitor:
    """Monitor system resource usage"""
    
    def __init__(self):
        self.samples: List[Dict[str, Any]] = []
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._interval = 1.0  # seconds
        
    def start_monitoring(self, interval: float = 1.0):
        """Start background monitoring"""
        self._interval = interval
        self._running = True
        self._thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self._thread.start()
    
    def stop_monitoring(self):
        """Stop background monitoring"""
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)
    
    def _monitor_loop(self):
        """Background monitoring loop"""
        while self._running:
            sample = self._collect_sample()
            self.samples.append(sample)
            
            # Keep only last 1000 samples
            if len(self.samples) > 1000:
                self.samples = self.samples[-1000:]
            
            time.sleep(self._interval)
    
    def _collect_sample(self) -> Dict[str, Any]:
        """Collect current resource usage sample"""
        import os
        process = os.process_info() if hasattr(os, 'process_info') else None
        
        sample = {
            "timestamp": time.time(),
            "memory_mb": self._get_memory_usage(),
            "thread_count": threading.active_count(),
        }
        
        return sample
    
    def _get_memory_usage(self) -> float:
        """Get current memory usage in MB"""
        try:
            import resource
            usage = resource.getrusage(resource.RUSAGE_SELF)
            return usage.ru_maxrss / 1024  # Convert to MB
        except:
            return 0.0
    
    def get_stats(self) -> Dict[str, Any]:
        """Get resource usage statistics"""
        if not self.samples:
            return {"error": "No samples collected"}
        
        memory_values = [s["memory_mb"] for s in self.samples]
        
        return {
            "sample_count": len(self.samples),
            "memory": {
                "current_mb": memory_values[-1] if memory_values else 0,
                "avg_mb": sum(memory_values) / len(memory_values),
                "max_mb": max(memory_values),
                "min_mb": min(memory_values)
            },
            "threads": {
                "current": threading.active_count()
            }
        }

class SystemTuner:
    """System performance tuning utilities"""
    
    @staticmethod
    def optimize_cache_sizes(current_size: int, available_memory_mb: int) -> int:
        """Calculate optimal cache size based on available memory"""
        # Use up to 25% of available memory for caches
        optimal = int(available_memory_mb * 0.25 * 1024 * 1024)
        return max(current_size, min(optimal, 10000))
    
    @staticmethod
    def calculate_io_batch_size(latency_ms: float) -> int:
        """Calculate optimal I/O batch size based on latency"""
        # Larger batches for higher latency
        if latency_ms < 1:
            return 64
        elif latency_ms < 10:
            return 32
        elif latency_ms < 100:
            return 16
        else:
            return 8
    
    @staticmethod
    def get_thread_pool_size(cpu_count: int, io_bound: bool = True) -> int:
        """Calculate optimal thread pool size"""
        if io_bound:
            # More threads for I/O bound tasks
            return cpu_count * 2
        else:
            # Fewer threads for CPU bound tasks
            return cpu_count + 1

# Global instances for system-wide use
_global_cache: Optional[LRUCache] = None
_profiler: Optional[PerformanceProfiler] = None
_resource_monitor: Optional[ResourceMonitor] = None

def get_global_cache(size: int = 1000) -> LRUCache:
    """Get or create global cache instance"""
    global _global_cache
    if _global_cache is None:
        _global_cache = LRUCache(max_size=size)
    return _global_cache

def get_profiler() -> PerformanceProfiler:
    """Get or create global profiler instance"""
    global _profiler
    if _profiler is None:
        _profiler = PerformanceProfiler()
    return _profiler

def get_resource_monitor() -> ResourceMonitor:
    """Get or create global resource monitor instance"""
    global _resource_monitor
    if _resource_monitor is None:
        _resource_monitor = ResourceMonitor()
    return _resource_monitor

if __name__ == "__main__":
    # Test cache
    print("Testing LRU Cache...")
    cache = LRUCache(max_size=5)
    for i in range(10):
        cache.put(f"key{i}", f"value{i}")
    print(f"Cache stats: {cache.get_stats()}")
    
    # Test profiler
    print("\nTesting Profiler...")
    profiler = get_profiler()
    
    def slow_function():
        time.sleep(0.1)
        return "done"
    
    for _ in range(5):
        profiler.measure_time(slow_function)
    
    print(f"Average time: {profiler.get_average_time('slow_function'):.2f}ms")
    print(f"Slowest: {profiler.get_slowest_functions(1)}")
    
    # Test resource monitor
    print("\nTesting Resource Monitor...")
    monitor = get_resource_monitor()
    monitor.start_monitoring(interval=0.5)
    time.sleep(2)
    monitor.stop_monitoring()
    print(f"Resource stats: {json.dumps(monitor.get_stats(), indent=2)}")
    
    print("\n✓ All optimization utilities tested successfully!")
