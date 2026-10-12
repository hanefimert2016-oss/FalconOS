# FalconBoot/1 — FalconOS V2'nin kendi GRUB'suz UEFI önyükleyicisi

## Gerçekte çalışan yol

UEFI x86_64 firmware (OVMF) -> El Torito FAT EFI system partition ->
`/EFI/BOOT/BOOTX64.EFI` (**FalconBoot**, kendi C + x86_64 assembly) ->
`/FALCONOS/KERNEL.BIN` -> GOP video + UEFI memory map ->
Multiboot2-compatible bounded bootinfo ->
ExitBootServices -> 4 GiB identity page tables + own GDT ->
FalconOS `long_start`.

**Bu GRUB, Limine veya shim değildir.** EFI yürütülebilir dosyası
`boot/falcon_uefi/loader.c` ve `handoff.S` kaynaklarından üretiliyor.
Kernel, EFI handoff için 32 MiB adresine linklenir; 64 MiB yer ayrılır.
Kernelin eski Multiboot2 giriş stub'ı derlenebilir olarak korunmuştur,
ancak FalconBoot bu giriş noktasını kullanmaz. `boot/grub.cfg` yoktur,
`make iso` GRUB paketi çağırmaz.

## Arch Linux'ta QEMU ile

Geliştirme bağımlılıkları: `base-devel nasm clang lld mtools xorriso
qemu-desktop edk2-ovmf python` ve yazı tipi üretimi için Pillow.

```sh
git clone -b v2-beta https://github.com/hanefimert2016-oss/FalconOS.git
cd FalconOS
make iso ENABLE_BOOT_GLB=0
make run-cdrom
```

`make run` varsayılan olarak QEMU'da ayrı güvenli test diski yaratır.
OVMF kod imajı sistem paketinden okunur, NVRAM dosyası `build/` içine
kopyalanır. Gerçek disklere yazmayın.

## Tamamlanmamış sınırlamalar

- **Yalnızca UEFI x86_64 / QEMU OVMF test edildi.** Legacy BIOS desteklenmiyor.
- Boot yükleyicisi FAT EFI bölümünden sabit boyutlu kernel imajı okur;
  henüz dinamik fiziksel ELF yükleyici veya relocate edilebilir imaj yok.
- 32 MiB tabanlı 64 MiB fiziksel RAM rezervasyonu ve 4 GiB kimlik
  eşlemeli sayfa tabloları küçük QEMU testleri için tasarlanmıştır.
  Modern fiziksel donanımın bütün MMIO/RAM aralıklarını kapsamaz.
- GOP desteği BGR32 moduyla sınırlıdır, framebuffer 4 GiB altında olmalıdır.
- Secure Boot anahtarları/imzası, iki aşamalı atomik güncelleme, geri alma,
  disk kurtarma ve fiziksel donanım sertifikasyonu yoktur.
- Bu, V2 beta işletim sistemini günlük kullanıma veya gerçek SSD
  kurulumuna uygun hâle getirmez.

## Doğrulama

`.github/workflows/v2-beta-version-qemu.yml` UEFI PE32+ derlemesini,
ISO'da GRUB olmamasını ve QEMU/OVMF içinde önce FalconBoot ekranının,
ardından masaüstü, Hakkında, Terminal ve Takvim uygulamalarının açılmasını
denetler. PNG dosyaları da ZIP dışında tek tek görüntülenebilir.
