# MQTT - Mosquitto

Bu klasör, SER-AI IoT projesinde MQTT mesajlaşma altyapısının kullanımını açıklar.

## Kullanılan Teknolojiler

- MQTT
- Eclipse Mosquitto

## MQTT Topic

`serai_mqtt/sensor/data`

## Görev

ESP32 üzerinden gönderilen sensör verileri, Mosquitto broker aracılığıyla MQTT abonelerine iletilir. Mesajlar JSON formatında görüntülenebilir.

## Mesajları Terminalde Görüntüleme

```bash
mosquitto_sub -h localhost -p 1883 -t "serai_mqtt/sensor/data" -v
```

Bu komut, yerel bilgisayarda çalışan broker üzerinden ilgili topic'e gelen mesajları görüntülemek için kullanılır.

## Not

Mosquitto ayrı bir yazılım olarak kurulur ve çalıştırılır. Bu klasör, projeye ait MQTT dokümantasyonunu içerir.