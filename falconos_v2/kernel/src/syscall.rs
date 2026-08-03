// FalconOS Syscall Interface
// Pure Rust - No Python

use x86_64::{
    instructions::interrupts,
    registers::model_specific::{Efer, EferFlags, LStar, Star},
    VirtAddr,
};

use crate::println;

// Syscall numbers for FalconOS
pub const SYS_READ: u64 = 0;
pub const SYS_WRITE: u64 = 1;
pub const SYS_OPEN: u64 = 2;
pub const SYS_CLOSE: u64 = 3;
pub const SYS_STAT: u64 = 4;
pub const SYS_FSTAT: u64 = 5;
pub const SYS_LSEEK: u64 = 6;
pub const SYS_MMAP: u64 = 9;
pub const SYS_MPROTECT: u64 = 10;
pub const SYS_MUNMAP: u64 = 11;
pub const SYS_BRK: u64 = 12;
pub const SYS_EXECVE: u64 = 59;
pub const SYS_EXIT: u64 = 60;
pub const SYS_FORK: u64 = 57;
pub const SYS_GETPID: u64 = 39;
pub const SYS_GETCWD: u64 = 79;
pub const SYS_CHDIR: u64 = 80;
pub const SYS_RENAME: u64 = 82;
pub const SYS_MKDIR: u64 = 83;
pub const SYS_RMDIR: u64 = 84;
pub const SYS_UNLINK: u64 = 87;
pub const SYS_READLINK: u64 = 89;
pub const SYS_ACCESS: u64 = 21;
pub const SYS_DUP: u64 = 32;
pub const SYS_DUP2: u64 = 33;
pub const SYS_PIPE: u64 = 29;
pub const SYS_SELECT: u64 = 27;
pub const SYS_NANOSLEEP: u64 = 35;
pub const SYS_GETUID: u64 = 102;
pub const SYS_GETGID: u64 = 104;
pub const SYS_GETEUID: u64 = 107;
pub const SYS_GETEGID: u64 = 108;
pub const SYS_IOCTL: u64 = 16;
pub const SYS_FCNTL: u64 = 72;
pub const SYS_FLOCK: u64 = 73;
pub const SYS_FSYNC: u64 = 74;
pub const SYS_GETTIMEOFDAY: u64 = 96;
pub const SYS_SENDFILE: u64 = 40;
pub const SYS_SOCKET: u64 = 41;
pub const SYS_CONNECT: u64 = 42;
pub const SYS_ACCEPT: u64 = 43;
pub const SYS_SENDTO: u64 = 44;
pub const SYS_RECVFROM: u64 = 45;
pub const SYS_BIND: u64 = 49;
pub const SYS_LISTEN: u64 = 50;
pub const SYS_SHUTDOWN: u64 = 48;

/// Initialize syscall interface using SYSCALL/SYSRET instructions
pub fn init() {
    println!("Initializing Syscall Interface...");
    
    // Set up syscall entry point
    // In real implementation, this would set LStar to the syscall handler address
    unsafe {
        // Enable SYSCALL in long mode
        let mut efer = Efer::read();
        efer.insert(EferFlags::SYSTEM_CALL_EXTENSIONS);
        Efer::write(efer);
        
        // Set up segment selectors for syscall (SS and CS)
        // Star::write(
        //     SegmentSelector::new(3, PrivilegeLevel::Ring3), // SS3
        //     SegmentSelector::new(1, PrivilegeLevel::Ring0), // CS0
        //     SegmentSelector::new(3, PrivilegeLevel::Ring3), // CS3
        // ).unwrap();
        
        println!("Syscall interface configured");
    }
}

/// Syscall handler - dispatches to appropriate kernel function
#[no_mangle]
pub extern "C" fn syscall_handler(
    syscall_num: u64,
    arg1: u64,
    arg2: u64,
    arg3: u64,
    arg4: u64,
    arg5: u64,
    arg6: u64,
) -> u64 {
    match syscall_num {
        SYS_WRITE => sys_write(arg1 as usize, arg2 as *const u8, arg3 as usize),
        SYS_READ => sys_read(arg1 as usize, arg2 as *mut u8, arg3 as usize),
        SYS_EXIT => sys_exit(arg1 as i32),
        SYS_GETPID => sys_getpid(),
        SYS_GETCWD => sys_getcwd(arg1 as *mut u8, arg2 as usize),
        _ => {
            println!("Unknown syscall: {}", syscall_num);
            u64::MAX // Error
        }
    }
}

fn sys_write(fd: usize, buf: *const u8, count: usize) -> u64 {
    // Placeholder - real implementation would write to file descriptor
    if fd == 1 || fd == 2 { // stdout or stderr
        unsafe {
            for i in 0..count {
                let c = *buf.add(i);
                // Write to VGA buffer or serial port
                if c == b'\n' {
                    crate::println!();
                } else {
                    crate::print!("{}", c as char);
                }
            }
        }
        count as u64
    } else {
        u64::MAX // Error
    }
}

fn sys_read(_fd: usize, _buf: *mut u8, _count: usize) -> u64 {
    // Placeholder - real implementation would read from file descriptor
    0
}

fn sys_exit(code: i32) -> u64 {
    println!("Process exited with code: {}", code);
    loop {} // Halt current process
}

fn sys_getpid() -> u64 {
    // Placeholder - return fake PID for now
    1
}

fn sys_getcwd(_buf: *mut u8, _size: usize) -> u64 {
    // Placeholder - return current working directory
    0
}
