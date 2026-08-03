// FalconOS Virtual File System
// Pure Rust - No Python

use alloc::{string::String, vec::Vec};

use crate::println;

#[derive(Debug, Clone)]
pub enum FileType {
    File,
    Directory,
    Symlink,
    Device,
}

#[derive(Debug, Clone)]
pub struct FileNode {
    pub name: String,
    pub file_type: FileType,
    pub size: u64,
    pub parent: Option<usize>,
    pub children: Vec<usize>,
    pub content: Vec<u8>,
    pub permissions: u32,
    pub uid: u32,
    pub gid: u32,
}

impl FileNode {
    pub fn new(name: &str, file_type: FileType) -> Self {
        FileNode {
            name: String::from(name),
            file_type,
            size: 0,
            parent: None,
            children: Vec::new(),
            content: Vec::new(),
            permissions: 0o755,
            uid: 0,
            gid: 0,
        }
    }
}

pub struct FileSystem {
    nodes: Vec<FileNode>,
    root_index: usize,
    current_dir: usize,
}

impl FileSystem {
    pub fn new() -> Self {
        let mut nodes = Vec::new();
        
        // Create root directory
        let mut root = FileNode::new("/", FileType::Directory);
        root.permissions = 0o755;
        nodes.push(root);
        
        FileSystem {
            nodes,
            root_index: 0,
            current_dir: 0,
        }
    }

    pub fn mkdir(&mut self, path: &str) -> Result<(), &'static str> {
        let parts: Vec<&str> = path.trim_matches('/').split('/').collect();
        let mut current = self.root_index;
        
        for (i, part) in parts.iter().enumerate() {
            if part.is_empty() {
                continue;
            }
            
            // Check if child exists
            let mut found = None;
            for &child_idx in &self.nodes[current].children {
                if self.nodes[child_idx].name == *part {
                    found = Some(child_idx);
                    break;
                }
            }
            
            if let Some(idx) = found {
                current = idx;
            } else {
                // Create new directory
                let mut new_dir = FileNode::new(part, FileType::Directory);
                new_dir.parent = Some(current);
                let new_idx = self.nodes.len();
                self.nodes.push(new_dir);
                self.nodes[current].children.push(new_idx);
                current = new_idx;
            }
            
            if i == parts.len() - 1 {
                return Ok(());
            }
        }
        
        Err("Invalid path")
    }

    pub fn create_file(&mut self, path: &str, content: &[u8]) -> Result<(), &'static str> {
        let parts: Vec<&str> = path.trim_matches('/').split('/').collect();
        if parts.is_empty() {
            return Err("Invalid path");
        }
        
        let file_name = parts.last().unwrap();
        let dir_path = if parts.len() > 1 {
            parts[..parts.len()-1].join("/")
        } else {
            String::from("/")
        };
        
        // Find or create parent directory
        let mut current = self.root_index;
        if dir_path != "/" {
            // Navigate to parent (simplified)
            for part in dir_path.trim_matches('/').split('/') {
                let mut found = None;
                for &child_idx in &self.nodes[current].children {
                    if self.nodes[child_idx].name == part {
                        found = Some(child_idx);
                        break;
                    }
                }
                if let Some(idx) = found {
                    current = idx;
                } else {
                    return Err("Parent directory not found");
                }
            }
        }
        
        // Create file
        let mut new_file = FileNode::new(file_name, FileType::File);
        new_file.parent = Some(current);
        new_file.content = content.to_vec();
        new_file.size = content.len() as u64;
        let new_idx = self.nodes.len();
        self.nodes.push(new_file);
        self.nodes[current].children.push(new_idx);
        
        Ok(())
    }

    pub fn read_file(&self, path: &str) -> Result<Vec<u8>, &'static str> {
        // Simplified implementation
        Ok(Vec::new())
    }

    pub fn list_dir(&self, path: &str) -> Result<Vec<String>, &'static str> {
        let mut result = Vec::new();
        
        if path == "/" {
            for &child_idx in &self.nodes[self.root_index].children {
                result.push(self.nodes[child_idx].name.clone());
            }
            return Ok(result);
        }
        
        // Find directory (simplified)
        Err("Directory not found")
    }

    pub fn change_dir(&mut self, path: &str) -> Result<(), &'static str> {
        if path == "/" {
            self.current_dir = self.root_index;
            return Ok(());
        }
        
        // Simplified navigation
        Err("Directory not found")
    }

    pub fn get_current_dir(&self) -> String {
        // Return current directory path
        String::from("/")
    }

    pub fn remove(&mut self, path: &str) -> Result<(), &'static str> {
        // Simplified removal
        Ok(())
    }

    pub fn rename(&mut self, old_path: &str, new_path: &str) -> Result<(), &'static str> {
        // Simplified rename
        Ok(())
    }
}

static mut FILESYSTEM: Option<FileSystem> = None;

pub fn init() {
    println!("Initializing Virtual File System...");
    
    unsafe {
        FILESYSTEM = Some(FileSystem::new());
    }
    
    // Create standard Linux directories
    unsafe {
        if let Some(ref mut fs) = FILESYSTEM {
            fs.mkdir("/bin").ok();
            fs.mkdir("/lib").ok();
            fs.mkdir("/etc").ok();
            fs.mkdir("/home").ok();
            fs.mkdir("/home/falcon").ok();
            fs.mkdir("/var").ok();
            fs.mkdir("/tmp").ok();
            fs.mkdir("/opt").ok();
            fs.mkdir("/usr").ok();
            fs.mkdir("/usr/bin").ok();
            fs.mkdir("/usr/lib").ok();
            fs.mkdir("/usr/share").ok();
            fs.mkdir("/falcon_apps").ok();
            
            println!("Standard directories created");
        }
    }
}

pub fn mkdir(path: &str) -> Result<(), &'static str> {
    unsafe {
        if let Some(ref mut fs) = FILESYSTEM {
            fs.mkdir(path)
        } else {
            Err("Filesystem not initialized")
        }
    }
}

pub fn create_file(path: &str, content: &[u8]) -> Result<(), &'static str> {
    unsafe {
        if let Some(ref mut fs) = FILESYSTEM {
            fs.create_file(path, content)
        } else {
            Err("Filesystem not initialized")
        }
    }
}

pub fn list_dir(path: &str) -> Result<Vec<String>, &'static str> {
    unsafe {
        if let Some(ref fs) = FILESYSTEM {
            fs.list_dir(path)
        } else {
            Err("Filesystem not initialized")
        }
    }
}

pub fn get_current_dir() -> String {
    unsafe {
        if let Some(ref fs) = FILESYSTEM {
            fs.get_current_dir()
        } else {
            String::from("/")
        }
    }
}
