#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <base64.h>
#include <Preferences.h>
#include "icons.h"
#include "config.h"

// --- PIN DEFINITIONS ---
#define TFT_LED        5
#define BTN_VOL_UP     25 
#define BTN_VOL_DOWN   26 
#define BTN_PREV       32 
#define BTN_NEXT       33 
#define BTN_PLAY_PAUSE 27 
#define BTN_EXTRA      14 

// --- DESK THING COLOR PALETTE (Dynamic) ---
uint16_t BG_DARK = 0x0000;        // Will be extracted from album
uint16_t ACCENT_COLOR = 0x07FF;   // Will be extracted from album
uint16_t TEXT_PRIMARY = 0xFFFF;   // White
uint16_t TEXT_SECONDARY = 0x8410; // Gray
uint16_t PROGRESS_BG = 0x2104;    // Dark gray
uint16_t CARD_BG = 0x2104;        // Will be darker version of BG

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
WiFiClientSecure client;
Preferences prefs;

// Global State
String accessToken = "";
String refreshToken = "";
unsigned long tokenMillis = 0;
bool isPlaying = false;
String currentTrack = "Not Playing";
String currentArtist = "";
String playlistName = "Spotify";
String currentContextUri = "";
String albumArtUrl = "";
String lastAlbumArtUrl = "";
int currentVolume = 50;
int trackProgress = 0;    // 0-100 percentage
int trackDuration = 100;  // Will be updated from API

// Button Debouncing
unsigned long lastButtonPress[6] = {0, 0, 0, 0, 0, 0};
const unsigned long DEBOUNCE_DELAY = 300;

// Command Queue
struct SpotifyCommand {
  String endpoint;
  String method;
  int volumeParam;
};

const int MAX_QUEUE_SIZE = 10;
SpotifyCommand commandQueue[MAX_QUEUE_SIZE];
int queueHead = 0;
int queueTail = 0;
int queueSize = 0;

void setup() {
  Serial.begin(115200);
  client.setInsecure();
  
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  pinMode(BTN_VOL_UP, INPUT_PULLUP);
  pinMode(BTN_VOL_DOWN, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PLAY_PAUSE, INPUT_PULLUP);
  pinMode(BTN_EXTRA, INPUT_PULLUP);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(0x0000);
  
  // Initialize JPEG decoder - Scale 2 for 300x300 -> 150x150
  TJpgDec.setJpgScale(2);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
  
  showBootScreen();

  WiFi.begin(ssid, password);
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print(".");
    dots++;
    if (dots % 3 == 0) {
      tft.fillRect(100, 140, 120, 20, BG_DARK);
      tft.setCursor(120, 140);
      tft.print("Connecting");
    } else {
      tft.print(".");
    }
  }
  
  Serial.println("\nWiFi Connected");
  
  prefs.begin("spotify", false);
  refreshToken = prefs.getString("refresh_token", "");

  if (refreshToken != "") {
    showStatusScreen("Logging in...");
    if (refreshAccessToken()) {
      showStatusScreen("Syncing...");
      updateTrackInfo();
      showStatusScreen("Ready!");
      delay(1000);
      drawPlayerUI();
    } else {
      showSetupScreen();
    }
  } else {
    showSetupScreen();
  }

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();
}

void loop() {
  server.handleClient();

  if (refreshToken != "" && (millis() - tokenMillis > 3000000)) {
    refreshAccessToken();
  }

  handleButtons();
  processCommandQueue();
  
  static unsigned long lastUpdate = 0;
  if (accessToken != "" && (millis() - lastUpdate > 2000)) {
    updateTrackInfo();
    lastUpdate = millis();
  }
  
  // Simulate progress bar movement
  static unsigned long lastProgressUpdate = 0;
  if (isPlaying && (millis() - lastProgressUpdate > 1000)) {
    trackProgress = (trackProgress + 1) % 101;
    updateProgressBar();
    lastProgressUpdate = millis();
  }
}

// --- JPEG DECODER CALLBACK ---

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

// --- COLOR EXTRACTION ---

uint16_t rgb888to565(uint32_t rgb888) {
  uint8_t r = (rgb888 >> 16) & 0xFF;
  uint8_t g = (rgb888 >> 8) & 0xFF;
  uint8_t b = rgb888 & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void extractColorsFromAlbumArt() {
  uint32_t totalR = 0, totalG = 0, totalB = 0;
  uint32_t maxVibrance = 0;
  uint8_t bestR = 29, bestG = 185, bestB = 84; // Default Spotify Green
  int sampleCount = 0;
  
  // Sample pixels from the 150x150 album art (10,10 to 160,160)
  for (int y = 20; y < 150; y += 10) {
    for (int x = 20; x < 150; x += 10) {
      uint16_t color = tft.readPixel(x, y);
      
      // Convert RGB565 to RGB888
      uint8_t r = ((color >> 11) & 0x1F) << 3;
      uint8_t g = ((color >> 5) & 0x3F) << 2;
      uint8_t b = (color & 0x1F) << 3;
      
      totalR += r;
      totalG += g;
      totalB += b;
      sampleCount++;
      
      // Calculate Vibrance: (Max - Min) * Max 
      // This favors highly saturated, bright colors
      uint8_t maxC = max(r, max(g, b));
      uint8_t minC = min(r, min(g, b));
      uint32_t vibrance = (uint32_t)(maxC - minC) * maxC;
      
      if (vibrance > maxVibrance) {
        maxVibrance = vibrance;
        bestR = r;
        bestG = g;
        bestB = b;
      }
    }
  }
  
  // Calculate average for the background base
  uint8_t avgR = totalR / sampleCount;
  uint8_t avgG = totalG / sampleCount;
  uint8_t avgB = totalB / sampleCount;
  
  // BG_DARK: Dark version of the average (40% brightness)
  BG_DARK = rgb888to565(((avgR * 80 / 100) << 16) | ((avgG * 80 / 100) << 8) | (avgB * 80 / 100));
  
  // CARD_BG: Lighter than BG (60% brightness)
  CARD_BG = rgb888to565(((avgR * 60 / 100) << 16) | ((avgG * 60 / 100) << 8) | (avgB * 60 / 100));
  
  // ACCENT_COLOR: Use the most vibrant pixel found
  // If the art is very grayscale (low vibrance), fallback to a boosted average
  if (maxVibrance < 1000) {
    ACCENT_COLOR = rgb888to565((min(255, avgR * 2) << 16) | (min(255, avgG * 2) << 8) | min(255, avgB * 2));
  } else {
    ACCENT_COLOR = rgb888to565((bestR << 16) | (bestG << 8) | bestB);
  }
  
  // PROGRESS_BG: Visible average (30% brightness)
  PROGRESS_BG = rgb888to565(((avgR * 30 / 100) << 16) | ((avgG * 30 / 100) << 8) | (avgB * 30 / 100));
  
  // Always keep text white as per preference
  TEXT_PRIMARY = 0xFFFF;
  TEXT_SECONDARY = 0xFFFF;
  
  Serial.printf("Colors extracted - BG: %04X, Accent: %04X (Vibrance: %d)\n", BG_DARK, ACCENT_COLOR, maxVibrance);
}

void downloadAndDisplayAlbumArt(String url) {
  static uint8_t* albumArtBuffer = NULL;
  static size_t albumArtSize = 0;

  // Case 1: Redraw existing image from cache
  if (url == lastAlbumArtUrl && albumArtBuffer != NULL) {
     TJpgDec.drawJpg(10, 10, albumArtBuffer, albumArtSize);
     return;
  }

  if (url == "") return;

  // Case 2: Download new image
  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    int len = http.getSize();
    
    // Manage memory - free old buffer before allocating new
    if (albumArtBuffer) free(albumArtBuffer);
    albumArtBuffer = (uint8_t*)malloc(len);
    
    if (albumArtBuffer) {
      albumArtSize = len;
      WiFiClient* stream = http.getStreamPtr();
      int bytesRead = 0;
      
      while (http.connected() && bytesRead < len) {
        size_t available = stream->available();
        if (available) {
          int readBytes = stream->readBytes(albumArtBuffer + bytesRead, available);
          bytesRead += readBytes;
        }
      }
      
      // Draw to screen (needed for color extraction)
      TJpgDec.drawJpg(10, 10, albumArtBuffer, albumArtSize);
      
      // Extract colors from the displayed pixels
      delay(50); 
      extractColorsFromAlbumArt();
      
      lastAlbumArtUrl = url;
      Serial.println("Album art cached & colors extracted");
    }
  }
  http.end();
}

// --- UI DRAWING FUNCTIONS ---

void showBootScreen() {
  BG_DARK = 0x0000; // Black for boot
  ACCENT_COLOR = 0x07FF; // Cyan
  tft.fillScreen(BG_DARK);
  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(3);
  tft.setCursor(60, 100);
  tft.print("Desk Thing");
  tft.setTextSize(1);
  tft.setTextColor(TEXT_SECONDARY);
  tft.setCursor(80, 130);
  tft.print("Powered by Spotify");
}

void showStatusScreen(String message) {
  tft.fillScreen(BG_DARK);
  tft.setTextColor(TEXT_PRIMARY);
  tft.setTextSize(2);
  tft.setCursor(40, 110);
  tft.print(message);
}

void showSetupScreen() {
  BG_DARK = 0x0000;
  ACCENT_COLOR = 0x07FF;
  tft.fillScreen(BG_DARK);
  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(30, 60);
  tft.print("Setup Required");
  
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRIMARY);
  tft.setCursor(10, 100);
  tft.print("Visit on your phone:");
  
  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 120);
  tft.print("http://");
  tft.setCursor(10, 140);
  tft.println(WiFi.localIP());
}

void drawPlayerUI() {
  // 1. Download & Extract Colors (if new)
  if (albumArtUrl != "" && albumArtUrl != lastAlbumArtUrl) {
    downloadAndDisplayAlbumArt(albumArtUrl);
  }
  
  tft.fillScreen(BG_DARK);
  
  // Calculate darker background for controls (dim by 50%)
  uint16_t r = (BG_DARK >> 11) & 0x1F;
  uint16_t g = (BG_DARK >> 5) & 0x3F;
  uint16_t b = BG_DARK & 0x1F;
  uint16_t CTRL_BG = ((r / 2) << 11) | ((g / 2) << 5) | (b / 2);
  
  // Draw darker controls background at bottom (1/4th of screen = 60px)
  tft.fillRect(0, 180, 320, 60, CTRL_BG);
  
  // 2. Draw Art (from cache)
  if (albumArtUrl != "") {
    downloadAndDisplayAlbumArt(albumArtUrl);
  } else {
    // Placeholder 150x150
    tft.fillRoundRect(10, 10, 150, 150, 8, CARD_BG);
    tft.setTextColor(TEXT_SECONDARY);
    tft.setTextSize(1);
    tft.setCursor(65, 80);
    tft.print("ALBUM");
  }
  
  // Playlist/Context name
  tft.setTextColor(TEXT_SECONDARY);
  tft.setTextSize(1);
  tft.setCursor(170, 15);
  tft.print(playlistName);
  
  // Track name (Font 4)
  tft.setTextColor(TEXT_PRIMARY);
  int textX = 170;
  int textY = 40;
  int maxWidth = 140; 
  
  String remaining = currentTrack;
  if (remaining.length() > 35) remaining = remaining.substring(0, 35) + "...";
  
  int cursorX = textX;
  int cursorY = textY;
  
  for (int i = 0; i < remaining.length(); i++) {
    char c = remaining.charAt(i);
    int charWidth = tft.drawChar(c, cursorX, cursorY, 4);
    cursorX += charWidth;
    if (cursorX > textX + maxWidth - 10) {
      cursorX = textX;
      cursorY += 28;
      if (cursorY > 140) break; // Don't overlap controls/seekbar
    }
  }
  
  // Artist name (Font 2)
  cursorY += 30; 
  tft.setTextColor(TEXT_SECONDARY);
  String displayArtist = currentArtist;
  if (displayArtist.length() > 25) displayArtist = displayArtist.substring(0, 25) + "...";
  tft.drawString(displayArtist, textX, cursorY, 2);
  
  // Progress bar background (Full width at y=180)
  tft.fillRect(0, 180, 320, 4, PROGRESS_BG);
  
  drawControls();
  updateProgressBar();
}

void drawControls() {
  int y = 198; // Centered vertically in the 60px panel (180 + (60-24)/2)
  int spacing = 60;
  int startX = 20;
  
  tft.drawXBitmap(startX, y, icon_shuffle, 24, 24, 0xFFFF);
  tft.drawXBitmap(startX + spacing, y, icon_prev, 24, 24, 0xFFFF);
  
  if (isPlaying) {
    tft.drawXBitmap(startX + spacing * 2, y, icon_pause, 24, 24, 0xFFFF);
  } else {
    tft.drawXBitmap(startX + spacing * 2, y, icon_play, 24, 24, 0xFFFF);
  }
  
  tft.drawXBitmap(startX + spacing * 3, y, icon_next, 24, 24, 0xFFFF);
  tft.drawXBitmap(startX + spacing * 4, y, icon_heart, 24, 24, 0xFFFF);
}

void updateProgressBar() {
  // Clear/draw progress bar background (Full width)
  tft.fillRect(0, 180, 320, 4, PROGRESS_BG);
  
  // Draw filled portion (White)
  int progressWidth = (trackProgress * 320) / 100;
  if (progressWidth > 0) {
    tft.fillRect(0, 180, progressWidth, 4, 0xFFFF);
  }
}

void updatePlayPauseIcon() {
  uint16_t r = (BG_DARK >> 11) & 0x1F;
  uint16_t g = (BG_DARK >> 5) & 0x3F;
  uint16_t b = BG_DARK & 0x1F;
  uint16_t CTRL_BG = ((r / 2) << 11) | ((g / 2) << 5) | (b / 2);

  int startX = 20, spacing = 60, y = 198;
  tft.fillRect(startX + spacing * 2, y, 24, 24, CTRL_BG);
  
  if (isPlaying) {
    tft.drawXBitmap(startX + spacing * 2, y, icon_pause, 24, 24, 0xFFFF);
  } else {
    tft.drawXBitmap(startX + spacing * 2, y, icon_play, 24, 24, 0xFFFF);
  }
}

void showVolumeOverlay(int vol) {
  // Draw a temporary volume overlay
  tft.fillRoundRect(100, 90, 120, 50, 10, CARD_BG);
  tft.drawRoundRect(100, 90, 120, 50, 10, ACCENT_COLOR);
  
  tft.setTextSize(2);
  tft.setTextColor(0xFFFF);
  tft.setCursor(125, 100);
  tft.print("VOL");
  
  tft.setTextColor(0xFFFF);
  tft.setTextSize(3);
  tft.setCursor(125, 115);
  tft.print(String(vol) + "%");
}

// --- COMMAND QUEUE ---

void queueCommand(String endpoint, String method, int volumeParam = -1) {
  if (queueSize >= MAX_QUEUE_SIZE) {
    Serial.println("Queue full");
    return;
  }
  
  commandQueue[queueTail].endpoint = endpoint;
  commandQueue[queueTail].method = method;
  commandQueue[queueTail].volumeParam = volumeParam;
  
  queueTail = (queueTail + 1) % MAX_QUEUE_SIZE;
  queueSize++;
}

void processCommandQueue() {
  if (queueSize == 0) return;
  
  SpotifyCommand cmd = commandQueue[queueHead];
  queueHead = (queueHead + 1) % MAX_QUEUE_SIZE;
  queueSize--;
  
  if (cmd.volumeParam >= 0) {
    setVolume(cmd.volumeParam);
  } else {
    sendCommand(cmd.endpoint, cmd.method);
  }
}

// --- WEB SETUP ---

void handleRoot() {
  String scope = "user-modify-playback-state%20user-read-playback-state%20user-read-currently-playing%20playlist-read-private%20user-library-read";
  String authUrl = "https://accounts.spotify.com/authorize?client_id=" + String(clientId) +
                   "&response_type=code&redirect_uri=http://127.0.0.1:8888/callback&scope=" + scope;
                   
  String html = "<html><head><style>body{font-family:sans-serif;background:#000;color:#fff;text-align:center;padding:50px;}";
  html += "a{color:#1DB954;font-size:24px;text-decoration:none;}";
  html += "input[type=text]{width:80%;padding:15px;font-size:16px;border:2px solid #1DB954;background:#1a1a1a;color:#fff;border-radius:8px;}";
  html += "input[type=submit]{padding:15px 30px;font-size:18px;background:#1DB954;color:#000;border:none;border-radius:8px;cursor:pointer;}";
  html += "</style></head><body>";
  html += "<h1>🎵 Spotify Desk Thing Setup</h1>";
  html += "<p>1. <a href='" + authUrl + "' target='_blank'>Login to Spotify</a></p>";
  html += "<p>2. Copy the callback URL (127.0.0.1:8888)</p>";
  html += "<form action='/save' method='get'><input type='text' name='url' placeholder='Paste URL here'><br><br>";
  html += "<input type='submit' value='Save to Device'></form></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  String raw = server.arg("url");
  int start = raw.indexOf("code=") + 5;
  if (start > 5) {
    String code = raw.substring(start);
    if (code.indexOf("&") != -1) code = code.substring(0, code.indexOf("&"));
    
    showStatusScreen("Authenticating...");
    if (performTokenExchange(code)) {
      server.send(200, "text/plain", "✓ Success! Close this page.");
      showStatusScreen("Success!");
      delay(1500);
      drawPlayerUI();
    } else {
      server.send(500, "text/plain", "✗ Error. Try again.");
    }
  }
}

// --- SPOTIFY API ---

bool performTokenExchange(String code) {
  HTTPClient http;
  http.begin(client, "https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String auth = base64::encode(String(clientId) + ":" + String(clientSecret));
  http.addHeader("Authorization", "Basic " + auth);

  String postData = "grant_type=authorization_code&code=" + code + "&redirect_uri=http://127.0.0.1:8888/callback";
  int httpCode = http.POST(postData);

  if (httpCode == 200) {
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, http.getString());
    accessToken = doc["access_token"].as<String>();
    refreshToken = doc["refresh_token"].as<String>();
    prefs.putString("refresh_token", refreshToken);
    tokenMillis = millis();
    http.end();
    return true;
  }
  http.end();
  return false;
}

bool refreshAccessToken() {
  if (refreshToken == "") return false;
  HTTPClient http;
  http.begin(client, "https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization", "Basic " + base64::encode(String(clientId) + ":" + String(clientSecret)));
  
  String postData = "grant_type=refresh_token&refresh_token=" + refreshToken;
  int httpCode = http.POST(postData);
  if (httpCode == 200) {
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, http.getString());
    accessToken = doc["access_token"].as<String>();
    tokenMillis = millis();
    http.end();
    return true;
  }
  http.end();
  return false;
}

String fetchArtistName(String uri) {
  if (accessToken == "" || uri == "") return "ARTIST RADIO";
  int lastColon = uri.lastIndexOf(':');
  if (lastColon == -1) return "ARTIST RADIO";
  String id = uri.substring(lastColon + 1);
  
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/artists/" + id);
  http.addHeader("Authorization", "Bearer " + accessToken);
  int httpCode = http.GET();
  
  String name = "ARTIST RADIO";
  if (httpCode == 200) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, http.getString());
    name = doc["name"].as<String>() + " Radio";
  }
  http.end();
  return name;
}

String fetchPlaylistName(String uri) {
  if (accessToken == "" || uri == "") return "PLAYLIST";
  
  // Extract ID from uri: spotify:playlist:37i9dQZF1DXcBWIGoYBM3M
  int lastColon = uri.lastIndexOf(':');
  if (lastColon == -1) return "PLAYLIST";
  String id = uri.substring(lastColon + 1);
  
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/playlists/" + id + "?fields=name");
  http.addHeader("Authorization", "Bearer " + accessToken);
  int httpCode = http.GET();
  
  String name = "PLAYLIST";
  if (httpCode == 200) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, http.getString());
    name = doc["name"].as<String>();
  }
  http.end();
  return name;
}

void updateTrackInfo() {
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player");
  http.addHeader("Authorization", "Bearer " + accessToken);
  int httpCode = http.GET();

  if (httpCode == 200) {
    StaticJsonDocument<4096> doc;
    deserializeJson(doc, http.getString());
    
    if (doc.containsKey("item") && !doc["item"].isNull()) {
      String track = doc["item"]["name"].as<String>();
      String artist = doc["item"]["artists"][0]["name"].as<String>();
      bool playing = doc["is_playing"].as<bool>();
      
      // Extract volume from device info
      int vol = currentVolume;
      if (doc.containsKey("device") && !doc["device"].isNull()) {
        vol = doc["device"]["volume_percent"].as<int>();
      }
      
      // Extract Context Name (Playlist, Album, etc.)
      String newPlaylistName = playlistName;
      String newContextUri = "";
      
      if (doc.containsKey("context") && !doc["context"].isNull()) {
        newContextUri = doc["context"]["uri"].as<String>();
        String type = doc["context"]["type"].as<String>();
        
        if (newContextUri != currentContextUri) {
          if (type == "playlist") {
            newPlaylistName = fetchPlaylistName(newContextUri);
          } else if (type == "album") {
            newPlaylistName = doc["item"]["album"]["name"].as<String>();
          } else if (type == "artist") {
            newPlaylistName = fetchArtistName(newContextUri);
          } else if (type == "collection") {
            newPlaylistName = "Liked Songs";
          }
          currentContextUri = newContextUri;
        }
      } else {
        newPlaylistName = "Spotify";
        currentContextUri = "";
      }
      
      // Get album art URL (Medium size for better display)
      String newAlbumUrl = "";
      JsonArray images = doc["item"]["album"]["images"];
      if (images.size() > 1) {
        newAlbumUrl = images[1]["url"].as<String>(); // 300x300
      } else if (images.size() > 0) {
        newAlbumUrl = images[0]["url"].as<String>();
      }
      
      int progress = doc["progress_ms"].as<int>();
      int duration = doc["item"]["duration_ms"].as<int>();
      
      if (duration > 0) {
        trackProgress = (progress * 100) / duration;
      }

      if (track != currentTrack || artist != currentArtist || playing != isPlaying || newAlbumUrl != albumArtUrl || newPlaylistName != playlistName || vol != currentVolume) {
        currentTrack = track;
        currentArtist = artist;
        isPlaying = playing;
        albumArtUrl = newAlbumUrl;
        playlistName = newPlaylistName;
        currentVolume = vol;
        drawPlayerUI();
      }
    }
  } else if (httpCode == 204) {
    // Nothing playing
    if (currentTrack != "Not Playing") {
      currentTrack = "Not Playing";
      currentArtist = "";
      isPlaying = false;
      albumArtUrl = "";
      drawPlayerUI();
    }
  }
  http.end();
}

// --- BUTTON HANDLING ---

void handleButtons() {
  unsigned long now = millis();
  
  // Volume Up
  if (digitalRead(BTN_VOL_UP) == LOW && (now - lastButtonPress[0] > DEBOUNCE_DELAY)) {
    lastButtonPress[0] = now;
    currentVolume = min(currentVolume + 10, 100);
    queueCommand("volume", "PUT", currentVolume);
    showVolumeOverlay(currentVolume);
    delay(800); // Show overlay
    drawPlayerUI();
    Serial.println("Vol+ -> " + String(currentVolume));
  }
  
  // Volume Down
  if (digitalRead(BTN_VOL_DOWN) == LOW && (now - lastButtonPress[1] > DEBOUNCE_DELAY)) {
    lastButtonPress[1] = now;
    currentVolume = max(currentVolume - 10, 0);
    queueCommand("volume", "PUT", currentVolume);
    showVolumeOverlay(currentVolume);
    delay(800);
    drawPlayerUI();
    Serial.println("Vol- -> " + String(currentVolume));
  }
  
  // Previous
  if (digitalRead(BTN_PREV) == LOW && (now - lastButtonPress[2] > DEBOUNCE_DELAY)) {
    lastButtonPress[2] = now;
    queueCommand("previous", "POST");
    Serial.println("Previous");
  }
  
  // Next
  if (digitalRead(BTN_NEXT) == LOW && (now - lastButtonPress[3] > DEBOUNCE_DELAY)) {
    lastButtonPress[3] = now;
    queueCommand("next", "POST");
    Serial.println("Next");
  }
  
  // Play/Pause
  if (digitalRead(BTN_PLAY_PAUSE) == LOW && (now - lastButtonPress[4] > DEBOUNCE_DELAY)) {
    lastButtonPress[4] = now;
    queueCommand(isPlaying ? "pause" : "play", "PUT");
    isPlaying = !isPlaying;
    updatePlayPauseIcon();
    Serial.println(isPlaying ? "Play" : "Pause");
  }
  
  // Extra - Force refresh
  if (digitalRead(BTN_EXTRA) == LOW && (now - lastButtonPress[5] > DEBOUNCE_DELAY)) {
    lastButtonPress[5] = now;
    updateTrackInfo();
    Serial.println("Refresh");
  }
}

void sendCommand(String cmd, String method) {
  if (accessToken == "") return;
  
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/" + cmd);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Length", "0");
  
  if (method == "PUT") {
    http.PUT("");
  } else {
    http.POST("");
  }
  http.end();
}

void setVolume(int vol) {
  if (accessToken == "") return;
  
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/volume?volume_percent=" + String(vol));
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Length", "0");
  http.PUT("");
  http.end();
}
