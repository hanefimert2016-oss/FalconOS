# FalconOS V2 Beta — 12 Ekim 2026 iyilestirme notlari

Bu degisiklikler yalnizca `v2-beta` dalindadir. Ana surum `FalconOS-1-release` korunur.

## Kullaniciya gorunen yenilikler

- **Surum tutarliligi:** GRUB'un HD/FHD/2K secenekleri, masaustu V2 BETA rozeti,
  Hakkinda ekrani, `uname` ve `ver` ayni FalconOS 2.0 Beta surumunu gosterir.
- **Gercek Takvim:** Eski calisma-suresinden uydurulan 28 gunluk tablo kaldirildi.
  CMOS RTC ve secili saat dilimine gore tarihi, Gregorian artik yillari ve ay uzunluklarini
  dogru hesaplar. Launchpad > Tum Uygulamalar > Calendar/Takvim.
  Sol/sag ok veya ekrandaki PREV/NEXT butonlari ile ay degistir,
  `T` veya Home tusu ile bugune don.
- **Uyumlu tema:** Ust durum cubugu tum FalconOS temalarinda sistem paletiyle
  cizilir. Ekran kaydi acikken ust cubukta REC gostergesi bulunur.
- **Hakkinda durumu:** Algilanan RTL8139 ag kartini, FalconFS test hacmi/RAM
  oturumunu ve deneysel guvenlik sinirlarini ayri olarak gosterir.
- **`doctor` komutu:** Anlik, gercek ag/disk durumunu; yerel TLS'nin surum
  onayinin ve uygulama seviyesinde Ring 3 yalitiminin olmadigini bildirir.
- **Yardim tamponu:** Terminal `help` ciktilari buffer sinirini asmaz.
  Bozuk kaydedilmis vurgu rengi indeksi de palet tablosunu tasirmaz.
- **Guncelleme dogrulugu:** `update check`/`apply` sadece yerel katalog
  girdilerini isaretler. Bu komutlar yeni FalconOS cekirdegi indirmez veya
  mevcut OS'yi gercekten guncellemez. Kullaniciya oyle oldugu soylenmez.

## Testler

- `gcc -std=c11 -Wall -Wextra -Werror -Ikernel tests/calendar_math_test.c kernel/calendar_math.c -o /tmp/calendar-test && /tmp/calendar-test`
- `python3 -m unittest discover -s tests -p 'test_*.py' -v`
- `make iso ENABLE_BOOT_GLB=0 ENABLE_BEARSSL=0 EXTRA_CFLAGS=-DFALCON_QEMU_UI_GALLERY`
- GitHub Actions `v2-beta-version-qemu.yml`: GRUB etiketleri, gercek ISO,
  terminal komutlari, gercek RTC Takvim cizimi ve QEMU ekran goruntuleri.

## Gercek sinirlar / guvenlik

**Bu bir deneysel QEMU betasidir; ana Windows/Linux diskinizde kurup
kullanmayin.** Dogrudan fiziksel NVMe/AHCI disklere guvenli yukleme,
uygulama Ring 3 yalitimi, tam imzali native uygulama ekosistemi, genel
HTML/CSS/JS tarayici motoru, onaylanmis tamamen yerel TLS ve native MP4
dosyasi olusturma henuz tamamlanmamistir. ISO'nun derlenmesi bu
ozelliklerin var oldugunu kanitlamaz.

Bu dal icin otomatik GitHub Release etiketi veya varsayilan dal degisikligi
yapilmadi. 
