# 🦅 FalconOS v2.1 "Nexus"

![FalconOS Banner](https://img.shields.io/badge/FalconOS-v2.1--alpha-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Python](https://img.shields.io/badge/python-3.8+-blue)
![Build](https://img.shields.io/badge/build-passing-brightgreen)

**Gerçek bir işletim sistemi deneyimi - Python ile güçlendirilmiş**

FalconOS, modern işletim sistemi konseptlerini simüle eden, gelişmiş dosya yönetimi, çoklu görev desteği ve zengin terminal arayüzü sunan bir işletim sistemi simülasyonudur.

## ✨ Özellikler

### 🔧 Kernel
- **Bellek Yönetimi**: Sayfa tabanlı bellek yönetimi, allocation/deallocation
- **Process Yönetimi**: Çoklu görev, process oluşturma/sonlandırma
- **Zamanlayıcı**: Round Robin, FIFO, Priority, CFS politikaları
- **Thread Havuzu**: Worker thread'leri ile paralel işlem

### 📁 Dosya Sistemi
- Hiyerarşik dosya/dizin yapısı
- Dosya izinleri (chmod)
- File descriptor yönetimi
- Kalıcı depolama (JSON serialization)
- Dizin ağacı görünümü

### 💻 Terminal
- 20+ yerleşik komut
- Komut geçmişi
- Otomatik tamamlama (simülasyon)
- Renkli çıktı
- Pipeline desteği (geliştirme aşamasında)

### 🖥️ Uygulamalar
- **Terminal**: Gelişmiş komut satırı
- **Dosya Yöneticisi**: İkon/liste/detay görünümleri
- **Ayarlar**: Sistem yapılandırması
- **Web Tarayıcı**: Simüle edilmiş browsing

## 🚀 Kurulum

```bash
# Depoyu klonla
git clone https://github.com/falconos/falconos.git
cd falconos

# Gereksinimler (Python 3.8+)
python3 --version

# Başlat
python3 main.py
```

## 📖 Kullanım

### Temel Komutlar

```bash
# Sistemi başlat
python3 main.py

# Yardım
user@falconos:~$ help

# Dosya işlemleri
user@falconos:~$ ls /home/user
user@falconos:~$ mkdir /home/user/documents
user@falconos:~$ touch /home/user/test.txt
user@falconos:~$ cat /home/user/test.txt
user@falconos:~$ rm /home/user/test.txt

# Sistem bilgisi
user@falconos:~$ ps          # Process'leri listele
user@falconos:~$ top         # Sistem istatistikleri
user@falconos:~$ mem         # Bellek kullanımı
user@falconos:~$ tree        # Dosya sistemi ağacı

# Kaydet ve çık
user@falconos:~$ save
user@falconos:~$ exit
```

## 🏗️ Proje Yapısı

```
FalconOS/
├── kernel/
│   ├── core.py          # Temel kernel bileşenleri
│   └── process.py       # Process ve thread yönetimi
├── apps/
│   └── system_apps.py   # Sistem uygulamaları
├── config/              # Yapılandırma dosyaları
├── data/                # Kullanıcı verileri
├── logs/                # Sistem logları
├── tests/               # Test dosyaları
├── main.py              # Ana başlatıcı
└── README.md            # Bu dosya
```

## 🎯 Performans

- **Boot Süresi**: ~0.4 saniye
- **Bellek Kullanımı**: 256MB (yapılandırılabilir)
- **Maksimum Process**: 256
- **Sayfa Boyutu**: 4KB

## 🧪 Test

```bash
# Temel testler
python3 -m pytest tests/

# Manuel test
python3 -c "from kernel.core import *; print('✓ Core OK')"
python3 -c "from kernel.process import *; print('✓ Process OK')"
```

## 🛣️ Yol Haritası

### v2.1 Alpha (Mevcut)
- ✅ Temel kernel fonksiyonları
- ✅ Dosya sistemi
- ✅ Process yönetimi
- ✅ Terminal uygulaması
- ✅ Sistem servisleri

### v2.2 Beta (Planlanan)
- [ ] Ağ stack'i (TCP/IP simülasyonu)
- [ ] GUI Desktop Environment
- [ ] Wine entegrasyonu (Windows app desteği)
- [ ] Linux uygulama uyumluluğu
- [ ] Ekran görüntüsü alma
- [ ] Arka plan değiştirme
- [ ] Çoklu kullanıcı desteği
- [ ] Güvenlik modülü

### v3.0 RC
- [ ] Gerçek donanım sürücüleri
- [ ] USB desteği
- [ ] Audio/Video playback
- [ ] Package manager
- [ ] App store

## 🤝 Katkıda Bulunma

1. Fork edin
2. Feature branch oluşturun (`git checkout -b feature/amazing-feature`)
3. Commit yapın (`git commit -m 'Add amazing feature'`)
4. Push edin (`git push origin feature/amazing-feature`)
5. Pull Request açın

## 📄 Lisans

MIT License - Detaylar için [LICENSE](LICENSE) dosyasına bakın.

## 👥 Ekip

- **Lead Developer**: FalconOS Team
- **Kernel Architect**: Open Source Contributors
- **UI/UX Design**: Community

## 🙏 Teşekkürler

- Python topluluğuna
- Tüm open source katkı sağlayıcılarına
- FalconOS kullanıcılarına

---

**🦅 FalconOS - Özgürlüğün Uçuşu**

[Website](https://falconos.local) | [Documentation](https://docs.falconos.local) | [Discord](https://discord.gg/falconos)
