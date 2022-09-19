#include "Arduino.h"
#include "OBD9141.h"
#include <SPI.h>
#include <WiFi101.h>
#include <Adafruit_SSD1306.h>

#include "arduino_secrets.h"

// Arduino MKR boards serial pins are 13 and 14
#define RX_PIN 13
#define TX_PIN 14
#define VEHICLE_SPEED 0x0D   // OBD2 PIDs for
#define ENGINE_RPM 0x0C      // speed, engine speed and coolant temperature
#define TEMPERATURE 0x05     // (defined in standard SAE J1979)
#define MAX_WAIT_COUNT 3

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
int status = WL_IDLE_STATUS;

const char* host = "maker.ifttt.com";
const char* event_trigger_url = "/trigger/YOUR_EVENT_NAME/with/key/YOUR_IFTTT_KEY";  // Ifttt event name and user key added here

float avg_speed = 0;
int avg_temp = 0;
int avg_rpm = 0;

unsigned long speed_sum = 0;
unsigned long temp_sum = 0;
unsigned long rpm_sum = 0;

unsigned long speed_count = 0;
unsigned long temp_count = 0;
unsigned long rpm_count = 0;

Adafruit_SSD1306 display(-1);
OBD9141 k_line;

uint8_t get_speed_value()
{
    bool response = k_line.getCurrentPID(VEHICLE_SPEED, 1);
    if (response)
    {
        uint8_t sensor_value = k_line.readUint8();
        return sensor_value;
    }
    return 0;
}

uint16_t get_rpm_value()
{
    bool response = k_line.getCurrentPID(ENGINE_RPM, 2);
    if (response)
    {
        uint16_t sensor_value = k_line.readUint16();
        return sensor_value;
    }
    return 0;
}

uint8_t get_temp_value()
{
    bool response = k_line.getCurrentPID(TEMPERATURE, 1);
    if (response)
    {
        uint8_t sensor_value = k_line.readUint8();
        return sensor_value;
    }
    return 0;
}

void update_display(uint8_t speed, uint16_t rpm, uint8_t temperature)
{
    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0,0);
    display.print("Speed: ");
    display.print(speed);
    display.print(" km/h");  

    display.setCursor(0,8);
    display.print("Eng speed: ");
    display.print(rpm);
    display.print(" rpm");

    display.setCursor(0,16);
    display.print("Coolant temp: ");
    display.print(temperature);
    display.print(" C");

    display.display();
}

void setup()
{
    k_line.begin(Serial1, RX_PIN, TX_PIN);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    delay(2000);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.display();
    Serial.begin(9600);
}

void loop()
{
    display.clearDisplay();
    display.setCursor(0,1);
    display.print("ISO9141 Init...");
    display.display();
    bool init_success = k_line.init();

    if (init_success)
    {
        display.clearDisplay();
        display.setCursor(0,1);
        display.print("ISO9141 Init Success");
        display.display();
        delay(3000);

        while (!get_rpm_value()) // Wait until engine is running
        {
            delay(200);
        }

        while (1)
        {
            delay(200);
            uint16_t rpm = get_rpm_value() / 4;
            if (!rpm) // Engine turned off so send email of average driving statistics
            {
                if (connect_to_wifi())
                {
                    calculate_averages();
                    send_email();
                }
                reset_values();
                break;
            }
            rpm_count++;
            rpm_sum += rpm;

            delay(200);
            uint8_t temp = get_temp_value() - 40;
            if (temp > 0)
            {
                temp_count++;
                temp_sum += temp;
            }
            
            delay(200);
            uint8_t speed = get_speed_value();
            if (speed > 0)
            {
                speed_count++;
                speed_sum += speed;
            }

            update_display(speed, rpm, temp);
        }
    }
    else
    {
        display.clearDisplay();
        display.setCursor(0,1);
        display.print("ISO9141 Init Failed");
        display.display();
        delay(3000);
    }
}

void calculate_averages()
{
    avg_speed = (1.0 * speed_sum) / speed_count;
    avg_temp = temp_sum / temp_count;
    avg_rpm = rpm_sum / rpm_count;
}

void reset_values()
{
    avg_speed = 0;
    avg_temp = 0;
    avg_rpm = 0;
    speed_sum = 0;
    temp_sum = 0;
    rpm_sum = 0;
    speed_count = 0;
    temp_count = 0;
    rpm_count = 0;
}

bool connect_to_wifi()
{
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Trying to connect:");
    display.setCursor(0,8);
    display.print(ssid);
    display.display();

    int waitcount = 0;
    while (status != WL_CONNECTED)
    {
        status = WiFi.begin(ssid, pass);
        if (waitcount == MAX_WAIT_COUNT)
        {
            display.setCursor(0,16);
            display.print("Connection Failed");
            display.display();
            delay(3000);
            return 0;
        }
        waitcount++;
        delay(5000);
    }

    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Connected to: ");
    display.setCursor(0,8);
    display.print(ssid);
    display.display();

    delay(3000);
    return 1;
}

void send_email()
{
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Trying to connect:");
    display.setCursor(0,8);
    display.print(host);
    display.display();

    WiFiClient client;
    int waitcount = 0;
    while (!client.connect(host, 80))
    {
        if (waitcount == MAX_WAIT_COUNT)
        {
            display.setCursor(0,16);
            display.print("Connection Failed");
            display.display();
            delay(3000);
            return;
        }
        waitcount++;
        delay(5000);
    }

    display.setCursor(0,16);
    display.print("Connected");
    display.display();
    delay(3000);

    String avg_s = String(avg_speed, 1);
    String avg_r = String(avg_rpm);
    String avg_t = String(avg_temp);
    String data = "{\"value1\":\"" + avg_s + "\",\"value2\":\"" + avg_r + "\",\"value3\":\"" + avg_t + "\"}";

    client.print(String("POST ") + event_trigger_url + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    //"Connection: close\r\n" +
                    "Content-Type: application/json\r\n" +
                    "Content-Length: " + data.length() + "\r\n" +
                    "\r\n" +
                    data + "\n");

    delay(1000);
    if (client.connected()) 
    {
        client.stop();
    }
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Email sent");
    display.display();

    WiFi.disconnect();
    delay(3000);
}
