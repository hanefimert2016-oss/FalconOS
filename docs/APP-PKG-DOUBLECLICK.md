# FalconOS .app.pkg = kendi uygulama paketi

FalconOS **içinde** indirilen bir `.app.pkg` dosyasına iki kez
tıklanınca, işletim sistemi bunu gerçek dosya yöneticisinden
`FAPP/1` olarak doğrular ve yalnızca izin verilen uygulama komutlarını
kendi yerel yorumlayıcısıyla çalıştırır. Sonuç FalconOS Terminal
penceresinde görünür. `.exe`, ELF veya Linux programı çalıştırıldığı
iddia edilmez.

## Gerçek kullanım

1. FalconOS içerisinde **Keşfet (Marketplace)** açıp internetten
   doğrulanmış bir Release paketi indir. Kurulu paket `/home/falcon/apps`
   içinde saklanır, aynı `.app.pkg` **İndirilenler** klasöründe görünür.
2. İstersen Falco adres çubuğuna tam bir HTTPS `...app.pkg`
   bağlantısı yapıştırıp Enter'a bas. İsteğe bağlı host HTTPS
   ağ geçidi, CA doğrulaması ve güvenli HTTPS yönlendirmelerini yapar;
   gelen dosya doğrulandıktan sonra `/home/falcon/Downloads` içine
   kaydedilir. İndirme sırasında **çalıştırılmaz**.
3. **Dosyalar → Downloads** bölümünde dosyaya iki kez tıkla.
   İlgili paket yerel FalconOS Terminal'de açılır.
   Tek tık yalnızca seçer. Klavyede seçili dosya + Enter da açar.

## Desteklenen .app.pkg biçimi

```
FAPP/1
id=hello-world
name=Hello World
version=1.0.0
summary=Hello World on FalconOS

echo Hello from FalconOS
uname
```

4 KiB'a kadar ASCII manifest ve satır-başı komutlar; izinli komut
listesi kaynak kodundaki `market_line_allowed` fonksiyonudur.
Parola, imza ve güvenli yürütme için kaynak doğrulaması önemlidir:
Keşfet paketleri GitHub SHA-256 ile doğrulanır. Herhangi bir URL'den
indirilen dosya ise **imzalı veya güvenilir sayılmaz**; format
doğrulaması yalnızca bozuk/engellenmiş sözdizimini reddeder.

## Sınırlar

Bu sürümde FAPP/1 yorumlanan konsol uygulamasıdır. Tümüyle yerel
makine kodu / GUI süreçleri, `ring3` uygulama izolasyonu ve keyfi ELF
çalıştırma henüz sunulmamaktadır. Uzantıyı yeniden adlandırmak
kontrolleri geçirmez. Host TLS yardımcısı da Chromium değildir; Python
standart kütüphanesindeki SSL/HTTPS doğrulamasını kullanır.

**Linux .AppImage burada hedef değil.** Kullanıcının istediği
`.app.pkg` gerçek FalconOS konuk işletim sistemi içindir. Bu dalın
CI testi yalnızca bootable FalconOS ISO ve gerçek konuk görüntülerini
yayımlar. Önceki dalda oluşmuş Linux AppImage bunlardan bağımsızdır.
