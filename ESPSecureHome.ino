#include <WiFi.h>
#include <PsychicHttp.h>

const char* ap_ssid = "WIFI";
const char* ap_password = "PasswordWIFI";

#define PIR_PIN 14
#define RED_LED 17
#define GREEN_LED 13
#define BUZZER_PIN 16

bool armed = false;
bool triggered = false;
bool sirenActive = false;
unsigned long alarmStartTime = 0;
const unsigned long SIREN_MAX_DURATION = 3600000;
const unsigned long ALARM_TIMEOUT = 10000;
String userPin = "1234";
const String masterPin = "admin123"; // Пользователь говорит программисту пароль чтобы никто не смог поменять

PsychicHttpServer server;

const char index_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Home Security</title>
        <style>
            body { 
                font-family: Arial; 
                text-align: center; 
                margin-top: 50px; 
            }
            .btn { 
                font-size: 1.2em; 
                padding: 10px 20px; 
                margin: 10px; 
                cursor: pointer; 
            }
            .arm { 
                background-color: #4CAF50; 
                color: white; 
                border: none; 
                border-radius: 5px; 
            }
            .disarm { 
                background-color: #f44336; 
                color: white; 
                border: none; 
                border-radius: 5px; 
                font-size: 1.2em; 
                padding: 10px 20px; 
                margin: 10px; 
                cursor: pointer; 
            }
            .pin-panel, .master-panel { 
                margin-top: 20px; 
                border-top: 1px solid #ccc; 
                padding-top: 15px; 
            }
            input { 
                font-size: 1.2em; 
                padding: 5px; 
                margin: 5px; 
            }
            #status { 
                font-size: 1.5em; 
                margin: 20px; 
                padding: 10px; 
                border-radius: 5px; 
            }
            .note { 
                font-size: 0.8em; 
                color: gray; 
            }
        </style>
    </head>
    <body>
        <h1>Home Security</h1>
        <div id="status">Loading...</div>
        <button id="armBtn" class="btn arm">Arm Security</button>
        <button id="disarmBtn" class="btn disarm">Disarm</button>
        <div class="pin-panel">
            <input type="password" id="pinInput" placeholder="Enter PIN code">
            <button id="pinBtn">Submit PIN</button>
        </div>
        <div class="master-panel">
            <h3>Change User PIN (only when DISARMED)</h3>
            <input type="password" id="masterPinInput" placeholder="Master PIN">
            <input type="password" id="newPinInput" placeholder="New user PIN">
            <button id="changePinBtn">Change User PIN</button>
            <div class="note">Master PIN is fixed and known only to owner.</div>
        </div>
        <script>
            async function updateStatus() 
            {
                const response = await fetch('/state');
                const data = await response.json();
                const statusDiv = document.getElementById('status');
                statusDiv.innerHTML = data.message;
                if (data.triggered) 
                    statusDiv.style.backgroundColor = '#ffcccc';
                else if (data.armed) 
                    statusDiv.style.backgroundColor = '#ccffcc';
                else 
                    statusDiv.style.backgroundColor = '#eeeeee';
            }
            setInterval(updateStatus, 1000);

            document.getElementById('armBtn').onclick = async () => {
                await fetch('/arm', { method: 'POST' });
                updateStatus();
            };
            document.getElementById('disarmBtn').onclick = async () => {
                const pin = document.getElementById('pinInput').value;
                const response = await fetch('/checkpin?pin=' + encodeURIComponent(pin), { method: 'POST' });
                const text = await response.text();
                alert(text);
                document.getElementById('pinInput').value = '';
                updateStatus();
            };
            document.getElementById('pinBtn').onclick = async () => {
                const pin = document.getElementById('pinInput').value;
                const response = await fetch('/checkpin?pin=' + encodeURIComponent(pin), { method: 'POST' });
                const text = await response.text();
                alert(text);
                document.getElementById('pinInput').value = '';
                updateStatus();
            };
            document.getElementById('changePinBtn').onclick = async () => {
                const master = document.getElementById('masterPinInput').value;
                const newPin = document.getElementById('newPinInput').value;
                if (newPin.length > 0) 
                {
                    const response = await fetch('/setpin?master=' + encodeURIComponent(master) + '&newpin=' + encodeURIComponent(newPin), { method: 'POST' });
                    const result = await response.text();
                    alert(result);
                    document.getElementById('masterPinInput').value = '';
                    document.getElementById('newPinInput').value = '';
                    updateStatus();
                } 
                else
                    alert('Enter new user PIN');
            };
            updateStatus();
        </script>
    </body>
    </html>
)rawliteral";

void setupWebServer()
{
    server.on("/", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) 
    {
        String html = index_html;
        response->send(200, "text/html", html.c_str());
        return ESP_OK;
    });
    server.on("/state", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) 
    {
        String msg;
        if (sirenActive) 
            msg = "SIREN! Enter PIN code";
        else if (triggered) 
        {
            unsigned long remaining = (ALARM_TIMEOUT - (millis() - alarmStartTime)) / 1000;
            msg = "ALARM! Enter PIN (" + String(remaining) + " sec)";
        } 
        else if (armed) 
            msg = "Security ARMED";
        else 
            msg = "Security DISARMED";
        String json = "{\"armed\":" + String(armed) + ",\"triggered\":" + String(triggered) + ",\"sirenActive\":" + String(sirenActive) + ",\"message\":\"" + msg + "\"}";
        response->send(200, "application/json", json.c_str());
        return ESP_OK;
    });
    server.on("/arm", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) 
    {
        armed = true;
        triggered = false;
        sirenActive = false;
        noTone(BUZZER_PIN);
        digitalWrite(RED_LED, LOW);
        digitalWrite(GREEN_LED, HIGH);
        response->send(200, "text/plain", "OK");
        return ESP_OK;
    });
    server.on("/checkpin", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) 
    {
        PsychicWebParameter* pinParam = request->getParam("pin");
        String pin = pinParam ? pinParam->value() : "";
        if (pin == userPin) 
        {
            armed = false;
            triggered = false;
            sirenActive = false;
            noTone(BUZZER_PIN);
            digitalWrite(RED_LED, LOW);
            digitalWrite(GREEN_LED, LOW);
            tone(BUZZER_PIN, 2000, 100);
            delay(150);
            tone(BUZZER_PIN, 2000, 100);
            response->send(200, "text/plain", "PIN OK, alarm reset");
        }
        else
            response->send(200, "text/plain", "Wrong PIN");
        return ESP_OK;
    });
    server.on("/setpin", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) 
    {
        PsychicWebParameter* masterParam = request->getParam("master");
        PsychicWebParameter* newPinParam = request->getParam("newpin");
        String master = masterParam ? masterParam->value() : "";
        String newPin = newPinParam ? newPinParam->value() : "";

        if (armed) 
        {
            response->send(403, "text/plain", "Cannot change PIN while system is ARMED!");
            return ESP_OK;
        }
        if (master != masterPin) 
        {
            response->send(403, "text/plain", "Invalid master PIN!");
            return ESP_OK;
        }
        if (newPin.length() == 0) 
        {
            response->send(400, "text/plain", "New PIN cannot be empty");
            return ESP_OK;
        }
        userPin = newPin;
        response->send(200, "text/plain", "User PIN changed successfully");
        return ESP_OK;
    });
    server.begin();
}

void setup() 
{
    Serial.begin(115200);
    pinMode(PIR_PIN, INPUT);
    pinMode(RED_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, LOW);
    noTone(BUZZER_PIN);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ap_ssid, ap_password);
    
    Serial.print("Connecting to Wi-Fi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nConnected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    }
    else
        Serial.println("\nFailed to connect. Check SSID/password.");

    setupWebServer();
}

void loop() 
{
    static bool lastMotion = LOW;
    bool motion = digitalRead(PIR_PIN);

    if (armed && !triggered && motion == HIGH && lastMotion == LOW) 
    {
        triggered = true;
        alarmStartTime = millis();
        sirenActive = false;
        digitalWrite(RED_LED, HIGH);
        digitalWrite(GREEN_LED, LOW);
        noTone(BUZZER_PIN);
        tone(BUZZER_PIN, 1500, 80);
        Serial.println("Motion detected! Alarm triggered.");
    }
    lastMotion = motion;

    if (triggered && !sirenActive && (millis() - alarmStartTime >= ALARM_TIMEOUT)) 
    {
        sirenActive = true;
        Serial.println("Siren activated!");
    }

    if (sirenActive)
    {
        if (millis() - alarmStartTime >= ALARM_TIMEOUT + SIREN_MAX_DURATION)
        {
            sirenActive = false;
            noTone(BUZZER_PIN);
            Serial.println("Siren auto-disabled after timeout.");
        }
        else
        {
            static unsigned long lastFreqChange = 0;
            static int freq = 800;
            static int freqStep = 10;

            if (millis() - lastFreqChange > 10)
            {
                freq += freqStep;
                if (freq >= 1200 || freq <= 800)
                    freqStep = -freqStep;
                tone(BUZZER_PIN, freq);
                lastFreqChange = millis();
            }
        }
    }
    else
        noTone(BUZZER_PIN);
}
