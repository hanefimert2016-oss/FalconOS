# FalconOS AeroFlow V2 — Native shell (2026-10-12)

AeroFlow, FalconOS V2 Beta'nin Windows'tan aliskin olunan kolay kullanimi
koruyan fakat FalconOS'a ozgu bir gorunuse sahip **native** masaustu
tasarimidir. HTML mockup ya da Chromium sayfasi degil; kernel tarafinda
C ile gercek framebuffer'a cizilir.

## Kodlananlar

- `kernel/aeroflow.h`: palete dayali yumusak cam katmanlar, kartlar,
  butonlar, kontrast uyumlu vurgular.
- `kernel/launchpad.c`: solda kategorili, ustte anlik aramali,
  gercek uygulama kimliklerinden olusan 4 sutunlu baslatici;
  fare/klavye, sayfalama, pinleme ve F2 kapatma.
- `kernel/personal.c`: buyuk ekranlarda gercek RTC takvim karti,
  ag baglantisi ve firmware RAM bilgisi, fonksiyonel kisayollar,
  uygulama durumunu gosteren yuyen gorev cubugu, hafif Falcon-kanadi
  vektor duvar kagidi. Uydurma hava durumu veya CPU yuzdesi yok.
- `kernel/apps.c`: coklu pencere ciziminde baslik, minimize/maximize/
  kapat dugmeleri, renk ve odak bilgisi guncellendi. Mevcut tasima,
  yeniden boyutlandirma ve Alt+Tab korunur.
- Liquid, Light, Dark paletleri icin gercek QEMU ekran goruntuleri ve
  piksel degisimi kontrolleri. Nordic/Rose temalari da ayni PAL mekanizmasini
  kullanir, ancak bu degisiklikte ayri piksel testi eklenmemistir.

## Nasil kullanilir?

```bash
git clone -b v2-beta https://github.com/hanefimert2016-oss/FalconOS.git
cd FalconOS
make iso ENABLE_BOOT_GLB=0
make run-cdrom
```

**UEFI/OVMF x86_64 QEMU gereklidir.** Arch Linux'ta `edk2-ovmf` kurulu olmali.
F2 => AeroFlow baslatici, yaz => tum uygulamalarda arama,
Tab => kategoriler, Enter => calistir, F3 => secileni masaustune sabitle,
Esc => kapat. Dosya, Takvim, Ayarlar ve Codedium kartlari gercek
FalconOS uygulamalarini baslatir.

Tasarim asamalidir. Onceki AI tasarim resmi bir fikir referansi olup
gercek arayuzun birebir ekran goruntusu **degildir**; ekran goruntulerini
yalnizca QEMU'nun gercek framebuffer'indan aliriz.

## Sinirlar

Falco hâlâ tam Chromium web motoru degildir. Depolama, native TLS,
modern ekran karti suruculeri ve uygulama proses yalitimi gunluk
bilgisayar kullanimi icin tamamlanmadi. Falco UI degisimi bu eksikleri
ortadan kaldirmiyor. Fiziksel SSD'ye kurmayin; QEMU'da deneyin.
