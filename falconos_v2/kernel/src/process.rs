// FalconOS Process Scheduler
// Pure Rust - No Python

use alloc::vec::Vec;

use crate::println;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ProcessState {
    Running,
    Ready,
    Blocked,
    Terminated,
}

#[derive(Debug)]
pub struct Process {
    pub pid: u64,
    pub state: ProcessState,
    pub priority: u8,
    pub stack_pointer: u64,
    pub instruction_pointer: u64,
    pub name: [u8; 64],
}

impl Process {
    pub fn new(pid: u64, name: &str) -> Self {
        let mut process_name = [0u8; 64];
        let bytes = name.as_bytes();
        for (i, &b) in bytes.iter().enumerate() {
            if i >= 64 {
                break;
            }
            process_name[i] = b;
        }
        
        Process {
            pid,
            state: ProcessState::Ready,
            priority: 10,
            stack_pointer: 0,
            instruction_pointer: 0,
            name: process_name,
        }
    }
}

pub struct Scheduler {
    processes: Vec<Process>,
    current_pid: u64,
    next_pid: u64,
}

impl Scheduler {
    pub fn new() -> Self {
        Scheduler {
            processes: Vec::new(),
            current_pid: 0,
            next_pid: 1,
        }
    }

    pub fn add_process(&mut self, name: &str) -> u64 {
        let pid = self.next_pid;
        self.next_pid += 1;
        
        let process = Process::new(pid, name);
        self.processes.push(process);
        
        println!("Created process '{}' with PID {}", name, pid);
        pid
    }

    pub fn schedule(&mut self) {
        // Simple round-robin scheduler
        // In real implementation, this would save/restore context
        
        if self.processes.is_empty() {
            return;
        }

        // Find next ready process
        let mut next_idx = 0;
        for (i, proc) in self.processes.iter().enumerate() {
            if proc.state == ProcessState::Ready {
                next_idx = i;
                break;
            }
        }

        // Switch to next process
        self.current_pid = self.processes[next_idx].pid;
    }

    pub fn get_current_pid(&self) -> u64 {
        self.current_pid
    }

    pub fn terminate_process(&mut self, pid: u64) {
        for proc in &mut self.processes {
            if proc.pid == pid {
                proc.state = ProcessState::Terminated;
                println!("Terminated process with PID {}", pid);
                break;
            }
        }
    }
}

static mut SCHEDULER: Option<Scheduler> = None;

pub fn init() {
    println!("Initializing Process Scheduler...");
    
    unsafe {
        SCHEDULER = Some(Scheduler::new());
    }
    
    // Create initial idle process
    unsafe {
        if let Some(ref mut sched) = SCHEDULER {
            sched.add_process("idle");
            sched.add_process("init");
            sched.add_process("falcon_gui");
            sched.add_process("falconbridge");
        }
    }
    
    println!("Scheduler initialized with {} processes", unsafe {
        SCHEDULER.as_ref().unwrap().processes.len()
    });
}

pub fn create_process(name: &str) -> u64 {
    unsafe {
        if let Some(ref mut sched) = SCHEDULER {
            sched.add_process(name)
        } else {
            0
        }
    }
}

pub fn schedule_next() {
    unsafe {
        if let Some(ref mut sched) = SCHEDULER {
            sched.schedule();
        }
    }
}

pub fn get_current_pid() -> u64 {
    unsafe {
        if let Some(ref sched) = SCHEDULER {
            sched.get_current_pid()
        } else {
            0
        }
    }
}

pub fn terminate_process(pid: u64) {
    unsafe {
        if let Some(ref mut sched) = SCHEDULER {
            sched.terminate_process(pid);
        }
    }
}
