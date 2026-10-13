# FalconOS V2 Beta — Hardware Probe, Falco SemanticText/2, Insider Updates

## Gercek donanim durumlari

- `lspci` komutu artik FalconOS'un x86_64 PCI config alanindan
  **gercek** bus/device/function, vendor/device ID ve sinif alanlarini
  salt-okunur algilar. Sinir: en fazla 48 cihaz hafizada, 20 satir terminalde.
- Intel (8086), NVIDIA (10de), Realtek (10ec), VirtIO ve temel QEMU ID
  ureticileri gorunur. NVMe, AHCI ve xHCI controller siniflari ayirt edilir.
- Ayarlar > Suruculer'de PCI sayisi ve gercek klavye/fare/ATA istatistikleri
  birlikte gosterilir.
- GPU icin gercek Metal/Vulkan/D3D surucusu yoktur. Modern NVMe ve xHCI
  kaydettirilen PCI cihazlarinin **algilanmasi**, namespace I/O / USB
  HID / DMA suruculerinin tamamlandigi anlamina gelmez.
- Bu kodun guvenli yolu PCI identity register okumasidir; eklenen envanter
  fonksiyonu donanim denetleyicilerini etkinlestirmez veya DMA baslatmaz.
  Mevcut deneysel MMIO/NVMe testlerini fiziksel SSD'de calistirmayin.

## Falco SemanticText/2

- Inert, buffer limitli yeni C motoru: `kernel/falco_html_text.c`.
- `h1/h2/h3`, `p`, `li`, `br` ve genel blok sinirlarini korur.
- `&amp;`, `&lt;`, `&gt;`, `&quot;`, sayisal HTML karakter basvurularini cozer.
- `script`, `style`, `template`, `noscript`, `svg`, `iframe`
  icerigini **calistirmadan ve gostermeden** atlar.
- Gercek HOST-CA-dogrulanmis HTTPS metin sayfalarina baglandi. FalconOS.tech'in
  eski siteye ozel Contour cizimi ayri kalir.
- CSS cascade, layout tree, JavaScript runtime, DOM, cookies, canvas,
  video decode, GPU acceleration henüz **yok**. Bu bir tam web engine degil,
  guvenli semantik HTML metin okuyucu adimidir.

## Stable, Beta ve Alpha guncelleme programlari

- Ayarlar değil, **Sistem Güncellemeleri** uygulamasinda Stable / Beta /
  Alpha tuslari veya terminalden `update channel stable|beta|alpha`.
- Katilim ayari FalconFS dogrulanmis 0xFA test diskine kaydedilir. Mevcut
  kullanicilar varsayilan **Beta** kanalinda kalir. Alpha deneysel olabilir.
- Tum kanallar kendi sabit HTTPS manifestinden gelir:
  `updates/os/stable/latest.fosup`, `updates/os/latest.fosup`,
  `updates/os/alpha/latest.fosup`.
- Manifestteki surum tag'i ve ISO adi katilan kanala uymali. Stable > Beta >
  Alpha onceligi ayni surum numarasi icin gecerlidir. Otomatik downgrade
  sunulmaz.
- Beta kanalinda onaylanmis 2.0.2-beta ISO mevcuttur. Alpha / Stable
  manifestleri **henüz yayinlanmadi**, bu durumda `No published build`
  gorunur. Uydurma paket verilmez.
- Otomatik kontrol aciktir; otomatik kuruluma ayri izin gerekir,
  sistem kendiliginden yeniden baslatilmaz.
- **Gercek ISO degisimi yalnızca QEMU/OVMF'de**, host üzerinden HTTPS ve
  SHA-256 dogrulamasi yapilarak gerceklesir. Imaj, QEMU'nun virtual CD
  yuvasina alinir, host tarafinda atomik aktif-ISO isareti kaydedilir.
  Eski ISO kurtarma icin saklanir.
- Bu, tam imzali yerel disk/EFI A/B güncelleyici veya fiziksel PC
  kurulumu değildir. Stable kaynagi bu nedenle bilerek bos tutulur.

## Dogrulama

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
gcc -std=c11 -Wall -Wextra -Werror -Ikernel \
  tests/falco_html_test.c kernel/falco_html_text.c -o /tmp/falco-test
/tmp/falco-test
make iso ENABLE_BOOT_GLB=0 ENABLE_BEARSSL=0
make run
```

Gercek bilgisayara kurulum yapmayin. V2 Beta oncelikle OVMF/QEMU icin
dogrulanir. Fiziksel bilgisayar icin NVMe/USB/GPU surucu tamamlama,
ACPI/interrupt, ring3 izolasyon, geri-alma ve guvenli disk yukleme gereklidir.
