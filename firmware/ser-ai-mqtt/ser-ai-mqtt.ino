
// ------------------------------------------------------------
// 1. KÜTÜPHANELER
// ------------------------------------------------------------

#include <ArduinoJson.h>       // JSON formatında veri oluşturmak için
#include <WiFi.h>              // ESP32'nin Wi-Fi bağlantısı
#include <Wire.h>              // I2C haberleşmesi
#include <Adafruit_Sensor.h>   // Sensör kütüphanesi
#include <Adafruit_BME680.h>   // BME680 sensörü
#include <Preferences.h>       // ESP32 kalıcı hafızasısss
#include <PubSubClient.h>      // MQTT haberleşmesi


// ------------------------------------------------------------
// 2. Wi-Fi BİLGİLERİ
// ------------------------------------------------------------
const char* ssid = "_bb_";
const char* wifi_password = "20032031";


// ------------------------------------------------------------
// 3. MQTT BROKER BİLGİLERİ
// ------------------------------------------------------------

const char* mqtt_server = "172.20.10.3";
const int mqtt_port = 1883; // MQTT'nin standart portu


// ESP32'nin sensör verilerini göndereceği topic
const char* mqtt_topic = "serai_mqtt/sensor/data";


// ------------------------------------------------------------
// 4. PIN TANIMLAMALARI
// ------------------------------------------------------------

#define FAN_PIN 23
#define MQ3_PIN 34
#define MQ4_PIN 35
#define MQ135_PIN 32


// ------------------------------------------------------------
// 5. NESNELER & Değişkenler
// ------------------------------------------------------------
Preferences preferences; // ESP32'nin kalıcı hafızası
Adafruit_BME680 bme;    // BME680 sensörü
WiFiClient espClient;   // Wi-Fi bağlantısı için istemci
PubSubClient mqttClient(espClient); // MQTT istemcisi


int ornek_id;
int gonderim_sayaci = 0;
int zaman_dilimi = 0;   // 0 -> Sabah, 1 -> Öğlen, 2 -> Akşam
int stres_etiketi = 0;  // 0 -> Sera, 1 -> Kuraklık, 2 -> S_kontrol, 3 -> Aşırı Sulama, 4 ->Düşük NaCl, 5 -> Nacl_kontrol, 6 -> Yüksek NaCl

unsigned long seansBaslangic;


// ------------------------------------------------------------
// 6. KONTROL DEĞERLERİ
// ------------------------------------------------------------

int kontrolMQ3;
int kontrolMQ4;
int kontrolMQ135;

float kontrolGasRes;


// ------------------------------------------------------------
// 7. HAREKETLİ ORTALAMA AYARLARI
// ------------------------------------------------------------

// Son 10 ölçüm kullanılarak sensörlerdeki ani dalgalanmaları azaltmak için hareketli ortalama hesaplanacak.

const int FILTRE_BOYUTU = 10;

// MQ sensörlerinin son 10 ölçümünü tutan diziler
int mq3_liste[FILTRE_BOYUTU];
int mq4_liste[FILTRE_BOYUTU];
int mq135_liste[FILTRE_BOYUTU];

int filtre_indeksi = 0;     // Yeni verinin yazılacağı dizi konumu


const long INTERVAL = 5000; // 5 saniyede bir veri gönderilecek
unsigned long lastSendTime = 0; // Son veri gönderiminin zamanı


// ============================================================
// 8. KALICI HAFIZAYI SIFIRLAMA
// ============================================================

void hafizayiSifirla() {

  preferences.begin("SER-AI", false);  
  preferences.clear();
  preferences.end(); 


  Serial.println();
  Serial.println(">>> [SİSTEM] Kalıcı hafıza sıfırlandı.");
  Serial.println(">>> [SİSTEM] Örnek ID 1'den başlayacak.");
}


// ============================================================
// 11. Wi-Fi BAĞLANTISI
// ============================================================

void wifiBaglan() {

  Serial.println();
  Serial.println("---------------------------------");
  Serial.println("Wi-Fi bağlantısı başlatılıyor...");
  Serial.println("---------------------------------");


  WiFi.begin(ssid, wifi_password);  // ESP32'yi Wi-Fi ağına bağlama


  // Bağlantı gerçekleşene kadar bekleme
  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");
  }


  Serial.println();
  Serial.println("Wi-Fi bağlantısı başarılı!");


  // ESP32'nin aldığı IP adresini gösterme
  Serial.print("ESP32 IP Adresi: ");
  Serial.println(WiFi.localIP());
}


// ============================================================
// 12. MQTT BROKER'A BAĞLANMA
// ============================================================

void mqttBaglan() {

  // MQTT bağlantısı kurulana kadar denemeye devam ediyoruz.
  while (!mqttClient.connected()) {
    Serial.print("MQTT Broker'a bağlanılıyor... ");


    // ESP32 için benzersiz MQTT Client ID oluşturuyoruz.
    String clientID = "SERAI_ESP32_" + String(ornek_id);


    // MQTT Broker'a bağlanmayı deniyoruz.
    if (mqttClient.connect(clientID.c_str())) 
    {

      Serial.println("BAĞLANDI!");


      Serial.print("MQTT Topic: ");
      Serial.println(mqtt_topic);
    }

    // Bağlantı başarısızsa hata kodunu yazdır.
    else 
    {
      Serial.print("BAĞLANAMADI. Hata kodu: ");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}


// ============================================================
// 13. SETUP
// ============================================================

void setup() {

  // Seri haberleşmeyi başlat

  Serial.begin(115200);
  delay(1000);


  // ----------------------------------------------------------
  // FAN AYARI
  // ----------------------------------------------------------
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);  // Sistem başlarken fan kapalı



  // ----------------------------------------------------------
  // KALICI HAFIZA
  // ----------------------------------------------------------

  // hafizayiSifirla();

  preferences.begin("SER-AI", false);


  // Daha önce kaydedilen son örnek ID'sini al.Eğer kayıt yoksa 0 döndür.
  ornek_id = preferences.getInt("sonOrnek_id", 0);
  ornek_id++;


  // Yeni ID'yi kalıcı hafızaya kaydet
  preferences.putInt("sonOrnek_id", ornek_id);


  // Preferences alanını kapat
  preferences.end();


  Serial.println();
  Serial.println("->->->->->->->->->->->->->->->->->->");
  Serial.println("SER-AI SISTEM BASLATILIYOR");
  Serial.println("->->->->->->->->->->->->->->->->->->");


  Serial.print("Örnek ID: ");
  Serial.println(ornek_id);


  // ----------------------------------------------------------
  // Wi-Fi BAĞLANTISI
  // ----------------------------------------------------------

  wifiBaglan();


  // ----------------------------------------------------------
  // MQTT AYARLARI
  // ----------------------------------------------------------

  // MQTT Broker adresini ve portunu ayarla
  mqttClient.setServer(mqtt_server, mqtt_port );


  // Göndereceğimiz JSON verisinin rahatça sığması için. MQTT mesaj boyutunu 512 byte'a çıkarıyoruz.
  mqttClient.setBufferSize(512);


  // ----------------------------------------------------------
  // BME680 BAŞLATMA
  // ----------------------------------------------------------

  Serial.println();
  Serial.println("BME680 başlatılıyor...");


  if (!bme.begin()) 
  {

    Serial.println("BME680 bulunamadı!");

    // Sensör bulunamazsa burada bekle
    while (1) {

      delay(1000);
    }
  }


  Serial.println(
      "BME680 başarıyla başlatıldı."
  );


  // ----------------------------------------------------------
  // BME680 AYARLARI
  // ----------------------------------------------------------

  // Sıcaklık için 8x oversampling
  bme.setTemperatureOversampling(
      BME680_OS_8X
  );


  // Nem için 2x oversampling
  bme.setHumidityOversampling(
      BME680_OS_2X
  );


  // BME680 gaz sensörü ısıtıcısı
  bme.setGasHeater(320,150);


  // ----------------------------------------------------------
  // KONTROL DEĞERLERİNİN ALINMASI
  // ----------------------------------------------------------

  Serial.println();
  Serial.println("Kontrol değerleri hazırlanıyor...");


  // Sensörlerin başlangıçta stabil olması için bekle
  delay(5000);


  // BME680 ölçümünü gerçekleştir
  if (bme.performReading()) {


    // MQ sensörlerinin kontrol değerlerini al
    kontrolMQ3 = analogRead(MQ3_PIN);
    kontrolMQ4 = analogRead(MQ4_PIN);
    kontrolMQ135 = analogRead(MQ135_PIN);


    // BME680 gaz direncini kOhm cinsinden al
    kontrolGasRes = (bme.gas_resistance / 1000.0);


    Serial.println("Kontrol değerleri:");


    Serial.print("MQ3: ");
    Serial.println(kontrolMQ3);


    Serial.print("MQ4: ");
    Serial.println(kontrolMQ4);


    Serial.print("MQ135: ");
    Serial.println(kontrolMQ135);


    Serial.print("Gas Resistance: ");
    Serial.println(kontrolGasRes);
  }


  // ----------------------------------------------------------
  // HAREKETLİ ORTALAMA DİZİLERİNİN BAŞLATILMASI
  // ----------------------------------------------------------

  // Filtre dizilerini kontrol değerleriyle dolduruyoruz.
  // Böylece sistem ilk başladığında
  // 0 değerleri nedeniyle yanlış ortalama oluşmaz.

  for (int k = 0; k < FILTRE_BOYUTU; k++) 
  {

    mq3_liste[k] = kontrolMQ3;
    mq4_liste[k] = kontrolMQ4;
    mq135_liste[k] = kontrolMQ135;
  }

  filtre_indeksi = 0;   // Filtre indeksini sıfırla

  seansBaslangic = millis();  // Seans başlangıç zamanını kaydet

  // ----------------------------------------------------------
  // MQTT BAĞLANTISI
  // ----------------------------------------------------------

  mqttBaglan();


  Serial.println();
  Serial.println("---------------------------------");
  Serial.println("SER-AI SISTEM HAZIR");
  Serial.println("---------------------------------");

  Serial.println("Sensör verileri 5 saniyede bir MQTT üzerinden gönderilecek.");
}


// ============================================================
// 14. LOOP
// ============================================================

void loop() {

  // Sistemin çalışmaya başladığı andan itibaren geçen milisaniye
  unsigned long currentMillis = millis();


  // ----------------------------------------------------------
  // MQTT BAĞLANTISINI KONTROL ET
  // ----------------------------------------------------------

  // Broker bağlantısı kopmuşsa tekrar bağlanmayı deneme
  if (!mqttClient.connected()) {
    mqttBaglan();
  }

  mqttClient.loop();


  // ----------------------------------------------------------
  // BME680 ÖLÇÜMÜ
  // ----------------------------------------------------------

  // BME680'den yeni ölçüm alma
  if (!bme.performReading()) 
  {
    Serial.println("BME680 ölçüm hatası!");
    delay(1000);
    return;
  }

  // ----------------------------------------------------------
  // HAM SENSOR VERİLERİNİ OKU
  // ----------------------------------------------------------

  // MQ sensörlerinden analog değerleri okuma
  int rawMQ3 = analogRead(MQ3_PIN);
  int rawMQ4 = analogRead(MQ4_PIN);
  int rawMQ135 = analogRead(MQ135_PIN);

  // BME680'den gelen değerler
  float temperature = bme.temperature;
  float humidity = bme.humidity;
  float gasRes = bme.gas_resistance / 1000.0; //kOhm


  // ----------------------------------------------------------
  // HAREKETLİ ORTALAMA
  // ----------------------------------------------------------

  // Yeni ölçümleri filtre dizilerine ekleme
  mq3_liste[filtre_indeksi] = rawMQ3;
  mq4_liste[filtre_indeksi] = rawMQ4;
  mq135_liste[filtre_indeksi] = rawMQ135;


  // Toplam değerler
  long toplamMQ3 = 0;
  long toplamMQ4 = 0;
  long toplamMQ135 = 0;


  // Son 10 ölçümü topla

  for (int i = 0; i < FILTRE_BOYUTU; i++ ) 
  {
    toplamMQ3 += mq3_liste[i];
    toplamMQ4 += mq4_liste[i];
    toplamMQ135 += mq135_liste[i];
  }

  // Ortalama değerleri hesaplama
  int fMQ3 = toplamMQ3 / FILTRE_BOYUTU;
  int fMQ4 = toplamMQ4 / FILTRE_BOYUTU;
  int fMQ135 = toplamMQ135 / FILTRE_BOYUTU;

  filtre_indeksi++;  // Bir sonraki filtre konumuna geç


  // Dizi sonuna geldiysek başa dön
  if ( filtre_indeksi >= FILTRE_BOYUTU) {
    filtre_indeksi = 0;
  }


  // ----------------------------------------------------------
  // KONTROL DEĞERİNE GÖRE FARK HESAPLAMA
  // ----------------------------------------------------------

  // Ölçüm ile kontrol değeri arasındaki
  // normalize edilmiş farkı hesaplıyoruz.

  float mq3_delta =(float)(fMQ3 - kontrolMQ3) / (kontrolMQ3 + 1);
  float mq4_delta =(float)(fMQ4 - kontrolMQ4) / (kontrolMQ4 + 1);
  float mq135_delta =(float)(fMQ135 - kontrolMQ135) / (kontrolMQ135 + 1);


  // BME680 gaz direnci farkı
  float gas_delta = (gasRes - kontrolGasRes) / (kontrolGasRes + 0.1);

  // ----------------------------------------------------------
  // FAN KONTROLÜ
  // ----------------------------------------------------------

  // MQ135 800'den büyükse veya sıcaklık 35°C'den büyükse fan açılır.
  bool fanKontrol =( fMQ135 > 800 || temperature > 35 );

  // Fan durumunu fiziksel pine uygula
  digitalWrite(FAN_PIN, fanKontrol ? HIGH: LOW );


  // ----------------------------------------------------------
  // MQTT VERİ GÖNDERME KONTROLÜ
  // ----------------------------------------------------------

  // Son gönderimden itibaren 5 saniye geçtiyse yeni MQTT mesajı oluştur.
  if (currentMillis - lastSendTime >= INTERVAL) 
  {

    // Son gönderim zamanını güncelleme
    lastSendTime = currentMillis;

    gonderim_sayaci++;


    // Deneyin başlangıcından itibaren kaç saniye geçtiğini hesaplama
    unsigned long gecenSure = (currentMillis - seansBaslangic) / 1000;


    // --------------------------------------------------------
    // JSON OLUŞTURMA
    // --------------------------------------------------------
    StaticJsonDocument<512> json;    // MQTT ile göndereceğimiz JSON nesnesi

    json["ornek_id"] = ornek_id;

    // Deney başladıktan sonra geçen süre
    json["saniye_t"] = gecenSure;

    // MQTT gönderim sayısı
    json["gonderim_sayaci"] = gonderim_sayaci;


    // --------------------------------------------------------
    // HAM MQ DEĞERLERİ
    // --------------------------------------------------------

    json["mq3"] = rawMQ3;
    json["mq4"] = rawMQ4;
    json["mq135"] = rawMQ135;


    // --------------------------------------------------------
    // FİLTRELENMİŞ MQ DEĞERLERİ
    // --------------------------------------------------------

    json["mq3_filtered"] = fMQ3;
    json["mq4_filtered"] = fMQ4;
    json["mq135_filtered"] = fMQ135;


    // --------------------------------------------------------
    // KONTROL ORTAMINA GÖRE FARKLAR
    // --------------------------------------------------------

    json["mq3_delta"] = mq3_delta;
    json["mq4_delta"] = mq4_delta;
    json["mq135_delta"] = mq135_delta;
    json["gas_delta"] = gas_delta;


    // --------------------------------------------------------
    // BME680 VERİLERİ
    // --------------------------------------------------------

    json["temperature"] = temperature;
    json["humidity"] = humidity;
    json["gas_resistance"] = gasRes;


    // --------------------------------------------------------
    // FAN DURUMU
    // --------------------------------------------------------
    json["fan"] = fanKontrol;


    // --------------------------------------------------------
    // DİĞER PROJE BİLGİLERİ
    // --------------------------------------------------------

    json["zaman_dilimi"] = zaman_dilimi;
    json["label"] = stres_etiketi;


    // --------------------------------------------------------
    // JSON'U STRING HALİNE GETİR
    // --------------------------------------------------------

    // MQTT'nin gönderebileceği karakter dizisi

    char payload[512];

    // JSON'u payload içerisine yaz
    serializeJson(json, payload);


    // --------------------------------------------------------
    // MQTT PUBLISH
    // --------------------------------------------------------

    // JSON verisini belirlediğimiz
    // MQTT topic'ine gönder.

    bool basarili = mqttClient.publish(
            mqtt_topic,
            payload
        );


    // --------------------------------------------------------
    // SERİ MONİTÖR
    // --------------------------------------------------------

    Serial.println();

    Serial.println("=================================");
    Serial.println("MQTT VERİ GÖNDERİMİ");
    Serial.println("=================================");


    Serial.print("Gönderim No: ");
    Serial.println(gonderim_sayaci);


    Serial.print("Topic: ");
    Serial.println(mqtt_topic);


    Serial.print("Gönderim durumu: ");


    if (basarili){
        Serial.println("BAŞARILI");
    }

    else {

      Serial.println("BAŞARISIZ");
    }


    // --------------------------------------------------------
    // JSON VERİSİNİ GÖSTER
    // --------------------------------------------------------

    Serial.println();

    Serial.println("Gönderilen JSON:");
    Serial.println(payload);


    // --------------------------------------------------------
    // SENSOR PANELİ
    // --------------------------------------------------------

    Serial.println();
    Serial.println("----------- SENSOR PANEL -----------");

    Serial.print("MQ3 Raw: ");
    Serial.println(rawMQ3);

    Serial.print("MQ4 Raw: ");
    Serial.println(rawMQ4);


    Serial.print("MQ135 Raw: ");
    Serial.println(rawMQ135);


    Serial.print("MQ3 Filtered: ");
    Serial.println(fMQ3);


    Serial.print("MQ4 Filtered: ");
    Serial.println(fMQ4);


    Serial.print("MQ135 Filtered: ");
    Serial.println(fMQ135);


    Serial.print("MQ3 Delta: ");
    Serial.println(mq3_delta);


    Serial.print("MQ4 Delta: ");
    Serial.println(mq4_delta);


    Serial.print("MQ135 Delta: ");
    Serial.println(mq135_delta);


    Serial.print("Sıcaklık: ");
    Serial.print(temperature);
    Serial.println(" °C");


    Serial.print("Nem: ");
    Serial.print(humidity);
    Serial.println(" %");


    Serial.print("Gas Resistance: ");
    Serial.print(gasRes);
    Serial.println(" kOhm");


    Serial.print("Fan: ");

    if (fanKontrol) {
      Serial.println("AÇIK");
    }

    else {
      Serial.println("KAPALI");
    }

    Serial.println("------------------------------------");
  }


  // Loop'un çok hızlı dönmesini engellemek için
  delay(10);
}