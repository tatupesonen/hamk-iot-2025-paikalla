#include "mbed.h"
#include "MFRC522.h"
#include "ESP8266Interface.h"
#include <MQTTClientMbedOs.h>
#include <string.h>
#include <stdio.h>

DigitalOut Green(PA_12);
DigitalOut Red(PA_8);
Thread LEDThread;

EventFlags ledEvents;
#define LED_GREEN_ON_1S     (1 << 0)
#define LED_RED_ON_1S       (1 << 1)
#define BOTH_STROBE         (1 << 2)
#define LED_RED_QUICK       (1 << 3)

#define RC522_MOSI PA_7
#define RC522_MISO PA_6
#define RC522_SCK  PA_5
#define RC522_CS   PB_0
#define RC522_RST  PA_1

MFRC522 rfid(RC522_MOSI, RC522_MISO, RC522_SCK, RC522_CS, RC522_RST);

#define ESP8266_TX PA_9
#define ESP8266_RX PA_10

#define MQTT_SERVER "test.mosquitto.org"
#define MQTT_PORT 1883
const char *TOPIC          = "attendance/reader/RFID-0003";
const char *RESPONSE_TOPIC = "attendance/response/RFID-0003";


void ledController() {
    while (true) {
        uint32_t f = ledEvents.wait_any(
            LED_GREEN_ON_1S | LED_RED_ON_1S | BOTH_STROBE | LED_RED_QUICK
        );

        if (f & LED_GREEN_ON_1S) {
            Green = 1;
            ThisThread::sleep_for(1000ms);
            Green = 0;
        }
        if (f & LED_RED_ON_1S) {
            Red = 1;
            ThisThread::sleep_for(1000ms);
            Red = 0;
        }
        if (f & BOTH_STROBE) {
            for (int i = 0; i < 2; i++) {
                Green = Red = 1;
                ThisThread::sleep_for(100ms);
                Green = Red = 0;
                ThisThread::sleep_for(150ms);
            }
        }
        if (f & LED_RED_QUICK) {
            for (int i = 0; i < 2; i++) {
                Red = 1;
                ThisThread::sleep_for(50ms);
                Red = 0;
                ThisThread::sleep_for(150ms);
            }
        }
    }
}

void mqtt_message_arrived(MQTT::MessageData& md) {
    MQTT::Message& msg = md.message;

    char payload[128];
    int len = msg.payloadlen < 127 ? msg.payloadlen : 127;
    memcpy(payload, msg.payload, len);
    payload[len] = '\0';

    printf("RX: %s\n", payload);

    if (strstr(payload, "OK"))          ledEvents.set(LED_GREEN_ON_1S);
    else if (strstr(payload, "DENIED")) ledEvents.set(LED_RED_ON_1S);
    else if (strstr(payload, "ALREADY"))ledEvents.set(BOTH_STROBE);
    else if (strstr(payload, "NO"))     ledEvents.set(LED_RED_QUICK);
}

bool wifi_connect(ESP8266Interface &esp) {
    if (esp.get_connection_status() == NSAPI_STATUS_GLOBAL_UP)
        return true;

    printf("Connecting WiFi...\n");
    for (int i = 0; i < 10; i++) {
        if (esp.connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2) == 0) {
            SocketAddress ip;
            esp.get_ip_address(&ip);
            printf("WiFi OK: %s\n", ip.get_ip_address());
            return true;
        }
        ThisThread::sleep_for(1000ms);
    }
    return false;
}

bool mqtt_connect(ESP8266Interface &esp,
                  TCPSocket &sock,
                  MQTTClient &mqtt,
                  SocketAddress &broker) {

    if (!wifi_connect(esp)) return false;

    if (esp.gethostbyname(MQTT_SERVER, &broker, NSAPI_IPv4, "esp") != 0)
        return false;

    broker.set_port(MQTT_PORT);
    sock.close();
    sock.open(&esp);

    if (sock.connect(broker) != 0)
        return false;

    static char client_id[40];

    auto now = Kernel::Clock::now().time_since_epoch();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    sprintf(client_id, "rfid-%08lX", (unsigned long)ms);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = client_id;
    data.keepAliveInterval = 20;
    data.cleansession = 1;

    if (mqtt.connect(data) != 0)
        return false;

    mqtt.subscribe(RESPONSE_TOPIC, MQTT::QOS0, mqtt_message_arrived);
    printf("MQTT connected as %s\n", client_id);
    return true;
}

int main() {
    LEDThread.start(ledController);
    rfid.PCD_Init();

    ESP8266Interface esp(ESP8266_TX, ESP8266_RX);
    TCPSocket socket;
    MQTTClient mqtt(&socket);
    SocketAddress broker;

    while (!mqtt_connect(esp, socket, mqtt, broker)) {
        printf("MQTT retry...\n");
        ThisThread::sleep_for(2000ms);
    }

    Timer uid_timer;
    uid_timer.start();
    char last_uid[40] = {0};

    while (true) {
        if (mqtt.yield(10) != 0) {
            mqtt.disconnect();
            while (!mqtt_connect(esp, socket, mqtt, broker))
                ThisThread::sleep_for(2000ms);
        }

        if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
            ThisThread::sleep_for(50ms);
            continue;
        }

        char uid[40];
        int pos = 0;
        for (uint8_t i = 0; i < rfid.uid.size; i++)
            pos += sprintf(&uid[pos], "%02X", rfid.uid.uidByte[i]);
        uid[pos] = '\0';

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();

        if (strcmp(uid, last_uid) == 0 &&
            uid_timer.elapsed_time() < std::chrono::milliseconds(2000)) {
            continue;
        }

        strcpy(last_uid, uid);
        uid_timer.reset();

        char payload[64];
        sprintf(payload, "{\"rfid_tag\":\"%s\"}", uid);

        MQTT::Message msg;
        msg.payload = payload;
        msg.payloadlen = strlen(payload);
        msg.qos = MQTT::QOS0;

        if (mqtt.publish(TOPIC, msg) != 0) {
            printf("Failed publishing mqtt message\n");
            mqtt.disconnect();
        } else {
            printf("TX UID %s\n", uid);
        }
    }
}
