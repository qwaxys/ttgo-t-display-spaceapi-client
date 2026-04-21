#include <FS.h>
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

TFT_eSPI tft = TFT_eSPI();
WiFiManager wm;

const char* CONFIG_PATH = "/config.json";
const int X_PAD = 4;
const int STATUS_BAR_H = 19;
const int SPACE_LINE_H = 12;
const int MAX_SPACES = 10;

char hostname[64] = "ttgo-display";
String spaceapiUrls = "";

struct SpaceStatus {
    String name;
    int state; // 0=closed, 1=open, -1=undefined
};

SpaceStatus spaces[MAX_SPACES];
int spaceCount = 0;

bool isValidHostname(const char* name) {
    size_t len = strlen(name);
    if (len == 0 || len > 63) return false;
    if (!isalpha(name[0])) return false;
    if (name[len - 1] == '-') return false;
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(name[i]) && name[i] != '-') return false;
    }
    return true;
}

void loadConfig() {
    if (!SPIFFS.exists(CONFIG_PATH)) return;
    File configFile = SPIFFS.open(CONFIG_PATH, "r");
    if (!configFile) return;

    JsonDocument doc;
    if (deserializeJson(doc, configFile) == DeserializationError::Ok) {
        if (doc["hostname"].is<const char*>()) {
            const char* val = doc["hostname"];
            if (isValidHostname(val)) {
                strncpy(hostname, val, sizeof(hostname));
            }
        }
        if (doc["spaceapi"].is<const char*>()) {
            spaceapiUrls = doc["spaceapi"].as<const char*>();
        }
    }
    configFile.close();
}

void saveConfig() {
    JsonDocument doc;
    doc["hostname"] = hostname;
    doc["spaceapi"] = spaceapiUrls;
    File configFile = SPIFFS.open(CONFIG_PATH, "w");
    if (configFile) {
        serializeJson(doc, configFile);
        configFile.close();
    }
}

WiFiClientSecure secureClient;

void fetchSpaceApi(const String& url, SpaceStatus& space) {
    HTTPClient http;
    http.setTimeout(5000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (url.startsWith("https")) {
        secureClient.setInsecure(); // skip cert validation
        http.begin(secureClient, url);
    } else {
        http.begin(url);
    }

    int code = http.GET();
    Serial.printf("SpaceAPI %s -> HTTP %d\n", url.c_str(), code);

    if (code == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println(payload.substring(0, 200));

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err == DeserializationError::Ok) {
            space.name = doc["space"].as<String>();
            if (doc["state"]["open"].is<bool>()) {
                space.state = doc["state"]["open"].as<bool>() ? 1 : 0;
            } else {
                space.state = -1;
            }
            Serial.printf("  -> %s: %d\n", space.name.c_str(), space.state);
        } else {
            Serial.printf("  -> JSON error: %s\n", err.c_str());
            space.name = url;
            space.state = -1;
        }
    } else {
        Serial.printf("  -> HTTP error: %d\n", code);
        space.name = url;
        space.state = -1;
    }
    http.end();
}

void fetchAllSpaces() {
    spaceCount = 0;
    String urls = spaceapiUrls;
    urls.trim();
    if (urls.length() == 0) return;

    int start = 0;
    while (start < (int)urls.length() && spaceCount < MAX_SPACES) {
        int end = urls.indexOf('\n', start);
        if (end == -1) end = urls.length();

        String url = urls.substring(start, end);
        url.trim();
        if (url.length() > 0) {
            fetchSpaceApi(url, spaces[spaceCount]);
            spaceCount++;
        }
        start = end + 1;
    }
}

void initDisplay() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
}

void drawWifiIcon(int x, int y, uint16_t color) {
    tft.fillCircle(x, y, 1, color);
    for (int r = 4; r <= 10; r += 3) {
        for (float a = -1.2; a <= 1.2; a += 0.05) {
            int px = x + (int)(r * sin(a));
            int py = y - (int)(r * cos(a));
            tft.drawPixel(px, py, color);
        }
    }
}

void drawStatusBar(const char* ssid, const char* ip, bool connected) {
    uint16_t statusColor = connected ? TFT_GREEN : TFT_YELLOW;
    drawWifiIcon(10, 10, statusColor);

    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(statusColor, TFT_BLACK);
    tft.setCursor(26, 3);
    tft.print(ssid);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    int ipWidth = strlen(ip) * 6;
    tft.setCursor(240 - ipWidth, 3);
    tft.print(ip);

    tft.drawFastHLine(0, 18, 240, TFT_DARKGREY);
}

void drawSpaces() {
    // Clear area below status bar
    tft.fillRect(0, STATUS_BAR_H, 240, 135 - STATUS_BAR_H, TFT_BLACK);

    tft.setTextFont(1);
    tft.setTextSize(2);

    for (int i = 0; i < spaceCount; i++) {
        int y = STATUS_BAR_H + 2 + i * 18;
        if (y + 16 > 135) break;

        uint16_t color;
        switch (spaces[i].state) {
            case 1:  color = TFT_GREEN; break;
            case 0:  color = TFT_RED;   break;
            default: color = TFT_WHITE;  break;
        }

        tft.setTextColor(color, TFT_BLACK);
        tft.setCursor(X_PAD, y);
        tft.print(spaces[i].name);
    }
}

void showAPMode() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar(wm.getConfigPortalSSID().c_str(), WiFi.softAPIP().toString().c_str(), false);
}

void showConnected() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar(WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), true);
    drawSpaces();
}

void showConnecting() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(1);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(40, 55);
    tft.print("Connecting...");
}

WiFiManagerParameter hostnameParam(
    "hostname",
    "Hostname",
    "ttgo-display",
    63,
    " pattern='[a-zA-Z]([a-zA-Z0-9\\-]*[a-zA-Z0-9])?' "
    "title='Letters, digits, hyphens. Must start with a letter, must not end with a hyphen.'"
);

char spaceapiHtmlBuf[2048];
WiFiManagerParameter* spaceapiParam = nullptr;

void buildSpaceapiHtml() {
    snprintf(spaceapiHtmlBuf, sizeof(spaceapiHtmlBuf),
        "<br/><label for='spaceapi'>SpaceAPI</label>"
        "<small> One URL per line</small><br/>"
        "<textarea id='spaceapi' name='spaceapi' rows='4' "
        "style='width:100%%;box-sizing:border-box;'>%s</textarea>",
        spaceapiUrls.c_str());
}

unsigned long lastFetch = 0;
const unsigned long FETCH_INTERVAL = 60000; // 1 minute

void setup() {
    Serial.begin(115200);

    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
    }

    loadConfig();
    hostnameParam.setValue(hostname, 63);

    buildSpaceapiHtml();
    spaceapiParam = new WiFiManagerParameter(spaceapiHtmlBuf);

    initDisplay();
    showConnecting();

    std::vector<const char*> menu = {"wifi", "param", "info", "sep", "exit"};
    wm.setMenu(menu);
    wm.addParameter(&hostnameParam);
    wm.addParameter(spaceapiParam);

    wm.setAPCallback([](WiFiManager* wm) {
        showAPMode();
    });

    wm.setSaveParamsCallback([]() {
        const char* val = hostnameParam.getValue();
        if (isValidHostname(val)) {
            strncpy(hostname, val, sizeof(hostname));
            WiFi.setHostname(hostname);
        }

        if (wm.server->hasArg("spaceapi")) {
            spaceapiUrls = wm.server->arg("spaceapi");
        }

        saveConfig();
        buildSpaceapiHtml();
        Serial.println("Config saved. Hostname: " + String(hostname));
        Serial.println("SpaceAPI URLs: " + spaceapiUrls);

        fetchAllSpaces();
        showConnected();
    });

    if (!wm.autoConnect("TTGO-Setup")) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextFont(1);
        tft.setTextSize(2);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(40, 55);
        tft.print("WiFi Failed");
        delay(3000);
        ESP.restart();
    }

    WiFi.setHostname(hostname);
    fetchAllSpaces();
    showConnected();
    lastFetch = millis();
    Serial.println("Connected: " + WiFi.localIP().toString());

    wm.startWebPortal();
}

void loop() {
    wm.process();

    if (millis() - lastFetch >= FETCH_INTERVAL) {
        fetchAllSpaces();
        drawSpaces();
        lastFetch = millis();
    }
}
