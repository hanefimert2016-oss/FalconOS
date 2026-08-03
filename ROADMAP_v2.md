# FalconOS v2.0 Geliştirme Yol Haritası

## 📋 Genel Bakış

FalconOS v2.0, v1.x serisinin üzerine inşa edilen, performansı, genişletilebilirliği ve kullanılabilirliği önemli ölçüde artıran bir major sürümdür. Bu doküman, v2.0 Alpha'nın mevcut durumunu ve gelecek sürümler için planları detaylandırır.

---

## ✅ Tamamlanan Özellikler (v2.0 Alpha)

### 1. **Performans Optimizasyonları** (`kernel/perf.c`)

- **Cache-aware Memory Operations**: CPU cache line (64 byte) hizalı kopyalama ve doldurma işlemleri
- **Memory Pool Allocator**: Küçük tahsisatlar için hızlı, fragmentation-free havuz
  - 4KB havuz, 64-byte bloklar
  - Bitmap-tabanlı hızlı alokasyon/de-alokasyon
- **Optimized String/Memory Functions**:
  - `perf_memcpy()`: Cache-line başına 64-byte kopyalama, prefetch ile
  - `perf_memset()`: 8-byte paralel doldurma
  - `perf_strlen()`, `perf_strcmp()`: Vectorization-friendly döngüler
- **Branch Prediction Hints**: `likely()` / `unlikely()` makroları
- **Prefetching**: `prefetch_read()` / `prefetch_write()` ile veri önceden yükleme
- **Performance Statistics**: Aloksyon, memcpy, memset sayaçları

**Kullanım Örneği:**
```c
// Hızlı bellek tahsisi
void *ptr = perf_mem_alloc();
if (ptr) {
    // 64-byte blok kullan
    perf_mem_free(ptr);
}

// Optimize edilmiş kopyalama
perf_memcpy(dst, src, size);  // Cache-aware
```

---

### 2. **Virtual Filesystem Layer** (`kernel/vfs.c`)

- **Unified FS Interface**: Farklı dosya sistemleri için soyutlama katmanı
- **In-Memory Filesystem**: RAM-disk benzeri geçici dosya sistemi
  - Dizin ağacı yapısı (parent-child ilişkileri)
  - Dosya/dizin oluşturma, okuma, yazma, silme
  - Path resolution (`/home/user/file.txt` formatı)
- **File Operations**:
  - `vfs_mkdir()`: Dizin oluşturma
  - `vfs_create()`: Dosya oluşturma
  - `vfs_read()` / `vfs_write()`: Dosya I/O
  - `vfs_readdir()`: Dizin listeleme
  - `vfs_stat()`: Dosya bilgisi alma
  - `vfs_remove()`: Dosya/dizin silme
- **Mount Point Support**: Çoklu bağlama noktası altyapısı (8'e kadar)
- **File Descriptors**: Açık dosya takibi (32'ye kadar)
- **Permissions Model**: Basit Unix-style izinler (READ/WRITE/EXEC)

**Kullanım Örneği:**
```c
// VFS başlatma
vfs_init();

// Dizin ve dosya oluştur
vfs_mkdir("/home");
vfs_create("/home/test.txt");

// Dosyaya yaz
const char *data = "Merhaba FalconOS!";
vfs_write("/home/test.txt", data, k_strlen(data));

// Dosyadan oku
char buffer[256];
i32 bytes = vfs_read("/home/test.txt", buffer, sizeof(buffer));

// Dizin listele
char names[10][64];
i32 count = vfs_readdir("/home", names, 10);
```

---

### 3. **Version Management** (`kernel/version.h`)

- **Semantic Versioning**: MAJOR.MINOR.PATCH formatı (2.0.0)
- **Feature Flags**: Derleme zamanı özellik kontrolü
  - `FEATURE_PERF_OPT`: Performans optimizasyonları
  - `FEATURE_VFS`: Sanal dosya sistemi
  - `FEATURE_NET_STACK`: Ağ yığını
  - `FEATURE_FAST_BOOT`: Hızlı önyükleme
  - `FEATURE_SMP_READY`: SMP desteği (gelecek)
  - `FEATURE_MULTITASK`: Preemptive multitasking (gelecek)
- **Build Metadata**: Timestamp ve Git hash entegrasyonu

---

## 🚀 Gelecek Özellikler (v2.0 Beta → RC → Stable)

### Phase 1: Beta (v2.0.0 Beta)

#### A. Gelişmiş Dosya Sistemi
- [ ] **Disk-backed Persistence**: VFS'i ATA/SATA disklerle kalıcı hale getirme
- [ ] **Journaling**: Crash recovery için günlük tabanlı kayıt
- [ ] **File Compression**: Transparent sıkıştırma (LZ4/DEFLATE)
- [ ] **Extended Attributes**: Dosya metadata'sı (owner, created, modified)

#### B. Ağ Yığını Genişletme
- [ ] **TCP/IP Stack**: Temel TCP/UDP soketleri
- [ ] **DHCP Client**: Otomatik IP yapılandırması
- [ ] **DNS Resolver**: İsim çözümleme
- [ ] **HTTP Client**: Web istekleri için minimal implementasyon

#### C. USB Geliştirmeleri
- [ ] **USB 2.0 EHCI**: Yüksek hızlı USB desteği
- [ ] **USB Mass Storage**: Flash disk okuma/yazma
- [ ] **USB HID**: Klavye/fare hotplug desteği

---

### Phase 2: Release Candidate (v2.0.0 RC)

#### A. Preemptive Multitasking
- [ ] **Process Scheduler**: Round-robin + priority-based scheduling
- [ ] **Context Switching**: Task state segment (TSS) yönetimi
- [ ] **Inter-Process Communication (IPC)**: Pipes, message queues, shared memory
- [ ] **Process Isolation**: Her process için ayrı adres alanı

#### B. SMP (Symmetric Multi-Processing)
- [ ] **APIC Initialization**: Local APIC ve I/O APIC kurulumu
- [ ] **CPU Hotplugging**: Runtime CPU ekleme/çıkarma
- [ ] **Spinlocks**: Multi-core synchronization
- [ ] **Per-CPU Data**: Her CPU için özel veri alanları

#### C. Güvenlik
- [ ] **User/Kernel Mode Separation**: Ring 0 / Ring 3 izolasyonu
- [ ] **Access Control Lists (ACL)**: Detaylı dosya izinleri
- [ ] **Secure Boot**: İmzalı kernel doğrulama
- [ ] **Address Space Layout Randomization (ASLR)**: Exploit zorlaştırma

---

### Phase 3: Stable Release (v2.0.0)

#### A. Uygulama Çatısı
- [ ] **Dynamic Linking**: ELF shared libraries (.so dosyaları)
- [ ] **System Calls**: POSIX-compliant syscall interface
- [ ] **Package Manager 2.0**: Dependency resolution, versioning
- [ ] **GUI Toolkit**: Widget-based UI framework

#### B. Sürücü Desteği
- [ ] **PCI Express**: NVMe SSD desteği
- [ ] **GPU Drivers**: Basic 2D acceleration (Intel/AMD/NVIDIA)
- [ ] **Audio**: HD Audio (HDA) codec desteği
- [ ] **Network Cards**: Realtek, Intel Ethernet controllers

#### C. Dokümantasyon & Araçlar
- [ ] **Man Pages**: Komut satırı help sistemi
- [ ] **Debugger**: Kernel-level debugger (kgdb benzeri)
- [ ] **Profiler**: Performance profiling araçları
- [ ] **SDK**: Third-party geliştirme kit'i

---

## 📊 Performans Hedefleri

| Metrik | v1.x | v2.0 Alpha | v2.0 Stable Hedef |
|--------|------|------------|-------------------|
| Boot Time | ~700ms | ~500ms* | <300ms |
| Memory Footprint | ~150KB | ~244KB | <500KB (full features) |
| File I/O (seq) | N/A | ~50 MB/s* | >200 MB/s |
| Context Switch | N/A | N/A | <1μs |
| Network Throughput | N/A | N/A | >900 Mbps |

\* *Tahmini değerler, gerçek donanımda test edilmeli*

---

## 🔧 Build & Test

### Mevcut Build
```bash
cd /workspace
make clean
make iso          # FalconOS.iso oluşturur (~244KB)
```

### Test Ortamı Gereksinimleri
```bash
# Minimum gereksinimler (QEMU için)
make run RAM=2048 CPUS=2 VRAM=64

# Persistent disk ile
make run-disk

# Headless test (CI/CD için)
make run-headless
```

### Unit Test Framework (Gelecek)
```c
// kernel/tests.c örneği (planlanan)
TEST(perf_memcpy_test) {
    u8 src[256], dst[256];
    perf_memset(src, 0xAB, sizeof(src));
    perf_memcpy(dst, src, sizeof(src));
    ASSERT(memcmp(src, dst, sizeof(src)) == 0);
}

TEST(vfs_basic_test) {
    vfs_init();
    ASSERT(vfs_mkdir("/test") == true);
    ASSERT(vfs_create("/test/file.txt") == true);
    // ... cleanup
}
```

---

## 🐛 Bilinen Sorunlar

1. **QEMU KVM Hatası**: CI ortamında KVM modülü yok, TCG fallback yavaş
   - Geçici çözüm: `make run RAM=2048` ile daha az RAM

2. **perf_mem_free() Pointer Comparison**: Warning var, functional değil
   - Fix: `(u8 *)ptr` cast eklendi

3. **VFS Persistence**: Şu an sadece in-memory, reboot'da kaybolur
   - Plan: v2.0 Beta'da diskdb_save() ile entegrasyon

---

## 📝 Katkıda Bulunma

### Kod Standartları
- **Naming**: `snake_case` fonksiyonlar, `UPPER_CASE` makrolar
- **Comments**: Doxygen-style değil, açıklayıcı İngilizce/Türkçe
- **Error Handling**: NULL/-1 return değerleri, global errno yok
- **Memory**: Her alloc için explicit free, leak yok

### Pull Request Süreci
1. Feature branch oluştur: `git checkout -b feature/vfs-persistence`
2. Değişiklikleri yap ve test et
3. Commit mesajı: `[VFS] Add disk persistence layer`
4. PR aç ve review iste

---

## 📅 Zaman Çizelgesi (Tahmini)

| Milestone | Tarih | Durum |
|-----------|-------|-------|
| v2.0.0 Alpha | Q1 2025 | ✅ Tamamlandı |
| v2.0.0 Beta | Q2 2025 | 🔄 Devam ediyor |
| v2.0.0 RC | Q3 2025 | ⏳ Planlandı |
| v2.0.0 Stable | Q4 2025 | ⏳ Planlandı |

---

## 🎯 Sonuç

FalconOS v2.0 Alpha, temel performans ve filesystem altyapısını başarıyla ekledi. 
Önümüzdeki 6-9 ay içinde Beta ve RC sürümleriyle production-ready bir OS haline gelecek.

**Hedef**: Tek bir ISO'da hem günlük kullanım hem de geliştirme için ideal, 
hafif ama güçlü bir x86_64 işletim sistemi.

---

*Son güncelleme: $(date)*  
*FalconOS Development Team*
