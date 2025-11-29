#include "mbed.h"
#include "MFRC522.h"
#include <stdio.h>

// Kirjasto jolla voidaan käyttää RC522
#include "MFRC522.h"
// Wifiä varten
#include "ESP8266Interface.h"
#include <MQTTClientMbedOs.h>
#include <string.h>

// Green
DigitalOut Green(PA_12);
Thread LEDThread;

#define RC522_MOSI PA_7 // menee kiinni L432KC:ssa PA_7 / A6
#define RC522_MISO PA_6 // PA_6 / A5
#define RC522_SCK  PA_5 // PA_5 / A4
#define RC522_CS   PB_0 // NSS / CS, RC522 nimellä SDA - menee PB_0 / D3
#define RC522_RST  PA_1 // RST - menee PA_1 / A1

MFRC522 rfid(
    RC522_MOSI,
    RC522_MISO, 
    RC522_SCK, 
    RC522_CS, 
    RC522_RST
);

// ESP8266
#define ESP8266_TX PA_9
#define ESP8266_RX PA_10


#define MQTT_SERVER "test.mosquitto.org"
#define MQTT_PORT 1883



// Ledijuttuja, että voi tehdä eventtejä.
EventFlags ledEvents;
#define LED_EVENT_ON        (1 << 0)
#define LED_EVENT_OFF       (1 << 1)
#define LED_EVENT_ON_1S     (1 << 2)
#define LED_STROBE          (1 << 3)

/*
    ESP8266Interface esp(MBED_CONF_APP_ESP_TX_PIN, MBED_CONF_APP_ESP_RX_PIN);
    
    //Store device IP
    SocketAddress deviceIP;
    //Store broker IP
    SocketAddress MQTTBroker;
    
    TCPSocket socket;
    MQTTClient client(&socket);
    
    printf("\nConnecting wifi..\n");

    int ret = esp.connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);

    if(ret != 0)
    {
        printf("\nConnection error\n");
    }
    else
    {
        printf("\nConnection success\n");
    }

    esp.get_ip_address(&deviceIP);
    printf("IP via DHCP: %s\n", deviceIP.get_ip_address());
    
     // Use with IP
    //SocketAddress MQTTBroker(MBED_CONF_APP_MQTT_BROKER_IP, MBED_CONF_APP_MQTT_BROKER_PORT);
    
    // Use with DNS
    esp.gethostbyname(MBED_CONF_APP_MQTT_BROKER_HOSTNAME, &MQTTBroker, NSAPI_IPv4, "esp");
    MQTTBroker.set_port(MBED_CONF_APP_MQTT_BROKER_PORT);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
    data.MQTTVersion = 3;
    char *id = MBED_CONF_APP_MQTT_ID;
    data.clientID.cstring = id;


    socket.open(&esp);
    socket.connect(MQTTBroker);
    client.connect(data);
    printf("Connected to MQTT broker");

    AnalogIn  ain5(A0); 

     // C ja D-osuudet
    printf("Starting to send messages preloop\n");
    for(int i = 0; i < 10; i++) {
        printf("Starting to send messages\n");
        char out_data1[256] = "";
        uint16_t pinvalue = ain5.read_u16();
        float pot_mv = ain5.read();
        
        printf("Read sensor value\n");
        
         // Lisätty kohta A
        char buffer[64];
        sprintf(buffer, "{\"sensorValue\":%u}", (unsigned int)pinvalue);

        MQTT::Message msg;
        msg.qos = MQTT::QOS0;
        msg.retained = false;
        msg.dup = false;
        msg.payload = (void*)buffer;
        msg.payloadlen = strlen(buffer);

        

        client.publish(MBED_CONF_APP_MQTT_TOPIC, msg);
        printf("Sent a message to MQTT broker\n\r");
        // Sleep time must be less than TCP timeout
        // TODO: check if socket is usable before publishing
        ThisThread::sleep_for(1s);
    }
    printf("Closing connection\n");
    //client.yield(100);
    client.disconnect();

*/


void ledController() {
    while (true) {
        uint32_t flags = ledEvents.wait_any(
            LED_EVENT_ON | LED_EVENT_OFF | LED_EVENT_ON_1S | LED_STROBE
        );

        if (flags & LED_EVENT_ON) {
            Green = 1;
        }

        if (flags & LED_EVENT_OFF) {
            Green = 0;
        }

        if (flags & LED_EVENT_ON_1S) {
            Green = 1;
            ThisThread::sleep_for(1s);
            Green = 0;
        }

        if (flags & LED_STROBE) {
            Green = 1;
            ThisThread::sleep_for(100ms);
            Green = 0;
            ThisThread::sleep_for(150ms);
            Green = 1;
            ThisThread::sleep_for(100ms);
            Green = 0;
        }
    }
}

void strobe() {
    ledEvents.set(LED_STROBE);
}

int main()
{
    LEDThread.start(ledController);
    printf("LED thread started\n");
    // resetoidaan vanha state RC522
    printf("Resetting RC522 state\n");
    rfid.PCD_Reset();
    rfid.PCD_Init();

    printf("Initializing ESP8266...\n");
    ESP8266Interface esp(ESP8266_TX, ESP8266_RX);
    printf("Initialized ESP8266...\n");
    printf("Connecting to wifi\n");
    int ret = esp.connect("", "", NSAPI_SECURITY_WPA_WPA2);

    
    if(ret != 0)
    {
        printf("Connection error\n");
    }
    else
    {
        printf("Connection success\n");
    }

    SocketAddress deviceIP;
    TCPSocket socket;
    SocketAddress MQTTBroker;
    MQTTClient mqtt(&socket);
    


    esp.get_ip_address(&deviceIP);
    printf("IP via DHCP: %s\n", deviceIP.get_ip_address());
    esp.gethostbyname(MQTT_SERVER, &MQTTBroker, NSAPI_IPv4, "esp");
    MQTTBroker.set_port(MQTT_PORT);
    strobe();

    // Connect to MQTT
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
    data.MQTTVersion = 3;
    char *id = "paikalla1";
    data.clientID.cstring = id;


    socket.open(&esp);
    socket.connect(MQTTBroker);
    mqtt.connect(data);
    

    while (true) {
        if ( ! rfid.PICC_IsNewCardPresent()) {
            ThisThread::sleep_for(200ms);
            continue;
        }

        // Select one of the cards
        if (rfid.PICC_ReadCardSerial()) {
            char uid[40];
            int pos = 0;

            for (uint8_t i = 0; i < rfid.uid.size; i++) {
                pos += sprintf(&uid[pos], "%02X", rfid.uid.uidByte[i]);
            }

            if (pos > 0) uid[pos - 1] = '\0';
            printf("Card UID: %s\n", uid);

            char buffer[64];
            sprintf(buffer, "{\"cardId\":%s}", uid);
            MQTT::Message msg;
            msg.qos = MQTT::QOS0;
            msg.retained = false;
            msg.dup = false;
            msg.payload = (void*)buffer;
            msg.payloadlen = strlen(buffer);
            mqtt.publish("tatutestaa/topic1", msg);
            strobe();
        }
    }   
}   

