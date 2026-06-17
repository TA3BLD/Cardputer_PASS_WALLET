#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <qrcode.h>
#include <vector>
#include "mbedtls/aes.h"
#include <esp_random.h>

USBHIDKeyboard Keyboard;

struct Credential {
    String title;
    String user;
    String pass;
};

std::vector<Credential> credentials;
int selectedIndex = 0;
int scrollOffset = 0;
const int itemsPerPage = 5;

enum AppState { STATE_LOGIN, STATE_LIST, STATE_DETAIL, STATE_QR, STATE_ADD_TITLE, STATE_ADD_USER, STATE_ADD_PASS_PROMPT, STATE_ADD_PASS, STATE_VERIFY_REVEAL };
AppState currentState = STATE_LOGIN;

const char* filePath = "/user/PASSWRD.json";
const String masterPassword = "1111";
const unsigned char aes_key[] = "1111111111111111";

String inputBuffer = "";
String loginBuffer = "";
Credential newCred;
bool isPasswordRevealed = false;

String generateRandomPassword() {
    String charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+-=[]{}|;:,.<>?";
    String password = "";
    
    uint32_t seed = esp_random(); 
    randomSeed(seed);

    for (int i = 0; i < 32; i++) {
        int randomIdx = random(0, charset.length());
        password += charset[randomIdx];
    }
    return password;
}

String encryptAES(String plainText) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, aes_key, 128);

    int originalLen = plainText.length();
    int paddedLen = originalLen + (16 - (originalLen % 16));
    unsigned char input[paddedLen];
    unsigned char output[paddedLen];

    memcpy(input, plainText.c_str(), originalLen);
    unsigned char padValue = paddedLen - originalLen;
    for (int i = originalLen; i < paddedLen; i++) input[i] = padValue;

    for (int i = 0; i < paddedLen; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
    }
    mbedtls_aes_free(&aes);

    String hexOut = "";
    for (int i = 0; i < paddedLen; i++) {
        if (output[i] < 16) hexOut += "0";
        hexOut += String(output[i], HEX);
    }
    return hexOut;
}

String decryptAES(String hexCipher) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, aes_key, 128);

    int len = hexCipher.length() / 2;
    if (len == 0 || len % 16 != 0) return "";

    unsigned char input[len];
    unsigned char output[len];

    for (int i = 0; i < len; i++) {
        String byteString = hexCipher.substring(i * 2, i * 2 + 2);
        input[i] = (unsigned char) strtol(byteString.c_str(), NULL, 16);
    }

    for (int i = 0; i < len; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, output + i);
    }
    mbedtls_aes_free(&aes);

    int padValue = output[len - 1];
    int unpaddedLen = len - padValue;
    
    if(unpaddedLen < 0 || unpaddedLen > len) return ""; 

    String plainText = "";
    for (int i = 0; i < unpaddedLen; i++) plainText += (char)output[i];
    
    return plainText;
}

void drawHeader(String text, uint16_t bgColor = 0x0A66) {
    M5Cardputer.Display.fillRect(0, 0, 240, 20, bgColor);
    M5Cardputer.Display.setTextColor(WHITE, bgColor);
    M5Cardputer.Display.setTextDatum(middle_center);
    M5Cardputer.Display.setTextSize(1.2);
    M5Cardputer.Display.drawString(text, 120, 10);
}

void drawBottomBar(String text, uint16_t bgColor = 0x1084, uint16_t textColor = 0xFFFF) {
    M5Cardputer.Display.fillRect(0, 120, 240, 15, bgColor);
    M5Cardputer.Display.setTextColor(textColor, bgColor);
    M5Cardputer.Display.setTextDatum(middle_left);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.drawString(text, 5, 127);
}

void drawLoginScreen(bool isError = false) {
    M5Cardputer.Display.fillScreen(BLACK);
    drawHeader("SISTEM GIRISI", 0x2145);
    
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setTextSize(1.1);
    
    if (isError) {
        M5Cardputer.Display.setTextColor(0xFF3333, BLACK);
        M5Cardputer.Display.drawString("Hatali Sifre!", 10, 35);
        delay(1500);
        M5Cardputer.Display.setTextColor(0xBBBB, BLACK);
        M5Cardputer.Display.drawString("Tekrar Deneyin:", 10, 35);
    } else {
        M5Cardputer.Display.setTextColor(0xBBBB, BLACK);
        M5Cardputer.Display.drawString("Ana Sifreyi Girin:", 10, 35);
    }
    
    M5Cardputer.Display.setTextColor(0x00FF00, BLACK);
    M5Cardputer.Display.setTextSize(1.2);
    String displayPass = "";
    for (int i = 0; i < loginBuffer.length(); i++) displayPass += "*";
    M5Cardputer.Display.drawString("> " + displayPass, 10, 55);
    
    drawBottomBar("[DEL] Sil  |  [ENTER] Gir");
}

void drawVerifyScreen(bool isError = false) {
    M5Cardputer.Display.fillScreen(BLACK);
    drawHeader("KIMLIK DOGRULAMA", 0xFB80);
    
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setTextSize(1.1);
    
    if (isError) {
        M5Cardputer.Display.setTextColor(0xFF3333, BLACK);
        M5Cardputer.Display.drawString("Hatali Sifre!", 10, 35);
        delay(1500);
        M5Cardputer.Display.setTextColor(0xBBBB, BLACK);
        M5Cardputer.Display.drawString("Tekrar Deneyin:", 10, 35);
    } else {
        M5Cardputer.Display.setTextColor(0xBBBB, BLACK);
        M5Cardputer.Display.drawString("Sifreyi Gormek Icin", 10, 30);
        M5Cardputer.Display.drawString("Ana Sifre Girin:", 10, 45);
    }
    
    M5Cardputer.Display.setTextColor(0x00FF00, BLACK);
    M5Cardputer.Display.setTextSize(1.2);
    String displayPass = "";
    for (int i = 0; i < loginBuffer.length(); i++) displayPass += "*";
    M5Cardputer.Display.drawString("> " + displayPass, 10, 70);
    
    drawBottomBar("[DEL] Geri  |  [ENTER] Onayla");
}

void drawList() {
    M5Cardputer.Display.fillScreen(BLACK);
    drawHeader("Sifreler (" + String(credentials.size()) + ")", 0x0A66);
    
    M5Cardputer.Display.setTextDatum(middle_left);
    M5Cardputer.Display.setTextSize(1.15);

    if (credentials.empty()) {
        M5Cardputer.Display.setTextColor(0xFDB4, BLACK);
        M5Cardputer.Display.drawString("Veri Yok!", 10, 50);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(0x888, BLACK);
        M5Cardputer.Display.drawString("[N] ile yeni sifre ekleyin", 10, 70);
        drawBottomBar("[N] Yeni Ekle");
        return;
    }

    for (int i = 0; i < itemsPerPage; i++) {
        int dataIndex = scrollOffset + i;
        if (dataIndex >= credentials.size()) break;
        
        int yPos = 25 + (i * 18);
        if (dataIndex == selectedIndex) {
            M5Cardputer.Display.fillRect(0, yPos, 240, 18, 0x049F);
            M5Cardputer.Display.setTextColor(0xFFFF, 0x049F);
        } else {
            M5Cardputer.Display.setTextColor(0xDDDD, BLACK);
        }
        M5Cardputer.Display.drawString(credentials[dataIndex].title, 10, yPos + 9);
    }
    
    drawBottomBar("[N] Yeni  |  [,/.] Sec  |  [ENTER] Ac");
}

void drawDetail() {
    M5Cardputer.Display.fillScreen(BLACK);
    drawHeader(credentials[selectedIndex].title, 0x2145);
    
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setTextSize(1.1);
    
    M5Cardputer.Display.setTextColor(0xBBBB, BLACK);
    M5Cardputer.Display.drawString("Kullanici:", 10, 30);
    M5Cardputer.Display.setTextColor(0x00FF00, BLACK);
    M5Cardputer.Display.setTextSize(1.2);
    M5Cardputer.Display.drawString(credentials[selectedIndex].user, 20, 45);
    
    M5Cardputer.Display.setTextSize(1.1);
    M5Cardputer.Display.setTextColor(0xBBBB, BLACK);
    M5Cardputer.Display.drawString("Sifre:", 10, 65);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    
    if (isPasswordRevealed) {
        M5Cardputer.Display.setTextColor(0x00FF00, BLACK);
        M5Cardputer.Display.setTextSize(1);
        String pw = credentials[selectedIndex].pass;
        // Ekrana sığması için 30 karakterde bir alt satıra geç (yaklaşık değer)
        if (pw.length() > 28) {
            M5Cardputer.Display.drawString(pw.substring(0, 28), 20, 80);
            M5Cardputer.Display.drawString(pw.substring(28), 20, 95);
        } else {
            M5Cardputer.Display.drawString(pw, 20, 80);
        }
    } else {
        M5Cardputer.Display.setTextColor(0xCCCC, BLACK);
        M5Cardputer.Display.setTextSize(1.2);
        String stars = "";
        for (int i = 0; i < credentials[selectedIndex].pass.length(); i++) stars += "*";
        // Yıldızlar da çok uzunsa kırp (Görsel bütünlük için)
        if (stars.length() > 28) stars = stars.substring(0, 28) + "...";
        M5Cardputer.Display.drawString(stars, 20, 80);
    }
    
    drawBottomBar("U-USB_us P-USB_pw G-Gor Q-QR -DEL-Geri", 0x1084, 0x00FF00);
}

void drawPassPromptScreen() {
    M5Cardputer.Display.fillScreen(BLACK);
    drawHeader("SIFRE SECIMI", 0x2145);
    
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setTextSize(1.1);
    
    M5Cardputer.Display.setTextColor(0xBBBB, BLACK);
    M5Cardputer.Display.drawString("Sifre Nasil Olusturulsun?", 10, 40);
    
    M5Cardputer.Display.setTextColor(0x00FF00, BLACK);
    M5Cardputer.Display.drawString("[M] Manuel Giris", 10, 65);
    M5Cardputer.Display.drawString("[R] Rastgele (32 Karakter)", 10, 85);
    
    drawBottomBar("[M] Manuel | [R] Rastgele | [DEL] Geri");
}

void typeDataUSB(String data, String label) {
    drawBottomBar(label + " Yaziliyor...", 0x1084, 0xF800);
    delay(500); 
    Keyboard.print(data);
    delay(500); 
    drawDetail();
}

void drawQR(String data) {
    M5Cardputer.Display.fillScreen(WHITE);
    QRCode qrcode;
    
    uint8_t qrcodeData[qrcode_getBufferSize(7)];
    qrcode_initText(&qrcode, qrcodeData, 7, 0, data.c_str());
    
    int scale = (qrcode.size * 3 > 135) ? 2 : 3;
    int offsetX = (240 - (qrcode.size * scale)) / 2;
    int offsetY = (135 - (qrcode.size * scale)) / 2;

    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                M5Cardputer.Display.fillRect(offsetX + (x * scale), offsetY + (y * scale), scale, scale, BLACK);
            }
        }
    }
}

void drawInputScreen(String prompt) {
    M5Cardputer.Display.fillScreen(BLACK);
    drawHeader(prompt, 0x2145);
    M5Cardputer.Display.setTextColor(0x00FF00, BLACK);
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setTextSize(1.2);
    M5Cardputer.Display.drawString("> " + inputBuffer, 10, 45);
    drawBottomBar("[DEL] Sil  |  [ENTER] Onayla");
}

void drawLoadingScreen(String text) {
    M5Cardputer.Display.fillScreen(BLACK);
    drawHeader("SISTEM BASLATILIYOR", 0x049F);
    M5Cardputer.Display.setTextColor(0x00FF00, BLACK);
    M5Cardputer.Display.setTextDatum(middle_center);
    M5Cardputer.Display.setTextSize(1.1);
    M5Cardputer.Display.drawString(text, 120, 67);
}

void saveToFile() {
    SD.remove(filePath);
    File file = SD.open(filePath, FILE_WRITE);
    if (file) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& cred : credentials) {
            JsonObject obj = arr.add<JsonObject>();
            obj["title"] = cred.title;
            obj["user"] = cred.user;
            obj["pass"] = encryptAES(cred.pass);
        }
        serializeJson(doc, file);
        file.close();
    }
}

void initSD() {
    drawLoadingScreen("SD Kart Baglaniliyor...");
    
    SPI.begin(40, 39, 14, 12);
    int retries = 0;
    while (!SD.begin(12, SPI, 25000000) && retries < 5) {
        delay(500);
        retries++;
    }
    
    if (!SD.begin(12, SPI, 25000000)) {
        M5Cardputer.Display.fillScreen(BLACK);
        drawHeader("HATA", 0xF800);
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setTextDatum(middle_center);
        M5Cardputer.Display.setTextSize(1.1);
        M5Cardputer.Display.drawString("SD Kart Bulunamadi!", 120, 50);
        M5Cardputer.Display.drawString("Yuvayi Kontrol Edin.", 120, 75);
        while(true) { delay(100); }
    }
    
    if (!SD.exists("/TA3BLD")) {
        SD.mkdir("/TA3BLD");
    }
    
    if (!SD.exists(filePath)) {
        File file = SD.open(filePath, FILE_WRITE);
        if (file) {
            file.print("[]");
            file.close();
        }
    }
    delay(1000);
}

void loadData() {
    File file = SD.open(filePath);
    if (!file) return;
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return;

    credentials.clear();
    for (JsonObject elem : doc.as<JsonArray>()) {
        Credential cred;
        cred.title = elem["title"].as<String>();
        cred.user = elem["user"].as<String>();
        cred.pass = decryptAES(elem["pass"].as<String>());
        credentials.push_back(cred);
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Keyboard.begin();
    USB.begin();
    
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextFont(1);
    
    initSD();
    drawLoginScreen();
}

void loop() {
    M5Cardputer.update();
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        
        if (currentState == STATE_LOGIN) {
            if (status.enter) {
                if (loginBuffer == masterPassword) {
                    loadData();
                    currentState = STATE_LIST;
                    drawList();
                } else {
                    loginBuffer = "";
                    drawLoginScreen(true);
                }
            } else if (status.del) {
                if (loginBuffer.length() > 0) {
                    loginBuffer.remove(loginBuffer.length() - 1);
                    drawLoginScreen();
                }
            } else {
                for (char c : status.word) loginBuffer += c;
                drawLoginScreen();
            }
        }
        else if (currentState == STATE_LIST) {
            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                if (selectedIndex > 0) selectedIndex--;
                if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
                drawList();
            }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                if (selectedIndex < (int)credentials.size() - 1) selectedIndex++;
                if (selectedIndex >= scrollOffset + itemsPerPage) scrollOffset = selectedIndex - itemsPerPage + 1;
                drawList();
            }
            if (status.enter && !credentials.empty()) {
                isPasswordRevealed = false;
                currentState = STATE_DETAIL;
                drawDetail();
            }
            if (M5Cardputer.Keyboard.isKeyPressed('n') || M5Cardputer.Keyboard.isKeyPressed('N')) {
                inputBuffer = "";
                currentState = STATE_ADD_TITLE;
                drawInputScreen("Baslik Girin:");
            }
        }
        else if (currentState == STATE_ADD_TITLE) {
            if (status.enter) {
                newCred.title = inputBuffer;
                inputBuffer = "";
                currentState = STATE_ADD_USER;
                drawInputScreen("Kullanici Adi Girin:");
            } else if (status.del) {
                if (inputBuffer.length() > 0) {
                    inputBuffer.remove(inputBuffer.length() - 1);
                    drawInputScreen("Baslik Girin:");
                } else {
                    currentState = STATE_LIST;
                    drawList();
                }
            } else {
                for (char c : status.word) inputBuffer += c;
                drawInputScreen("Baslik Girin:");
            }
        }
        else if (currentState == STATE_ADD_USER) {
            if (status.enter) {
                newCred.user = inputBuffer;
                inputBuffer = "";
                currentState = STATE_ADD_PASS_PROMPT;
                drawPassPromptScreen();
            } else if (status.del) {
                if (inputBuffer.length() > 0) {
                    inputBuffer.remove(inputBuffer.length() - 1);
                    drawInputScreen("Kullanici Adi Girin:");
                } else {
                    inputBuffer = newCred.title;
                    currentState = STATE_ADD_TITLE;
                    drawInputScreen("Baslik Girin:");
                }
            } else {
                for (char c : status.word) inputBuffer += c;
                drawInputScreen("Kullanici Adi Girin:");
            }
        }
        else if (currentState == STATE_ADD_PASS_PROMPT) {
            if (M5Cardputer.Keyboard.isKeyPressed('m') || M5Cardputer.Keyboard.isKeyPressed('M')) {
                inputBuffer = "";
                currentState = STATE_ADD_PASS;
                drawInputScreen("Sifre Girin:");
            } else if (M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R')) {
                newCred.pass = generateRandomPassword();
                credentials.push_back(newCred);
                saveToFile();
                currentState = STATE_LIST;
                selectedIndex = credentials.size() - 1;
                drawList();
            } else if (status.del) {
                inputBuffer = newCred.user;
                currentState = STATE_ADD_USER;
                drawInputScreen("Kullanici Adi Girin:");
            }
        }
        else if (currentState == STATE_ADD_PASS) {
            if (status.enter) {
                newCred.pass = inputBuffer;
                credentials.push_back(newCred);
                saveToFile();
                inputBuffer = "";
                currentState = STATE_LIST;
                selectedIndex = credentials.size() - 1;
                drawList();
            } else if (status.del) {
                if (inputBuffer.length() > 0) {
                    inputBuffer.remove(inputBuffer.length() - 1);
                    drawInputScreen("Sifre Girin:");
                } else {
                    currentState = STATE_ADD_PASS_PROMPT;
                    drawPassPromptScreen();
                }
            } else {
                for (char c : status.word) inputBuffer += c;
                drawInputScreen("Sifre Girin:");
            }
        }
        else if (currentState == STATE_DETAIL) {
            if (status.del) {
                currentState = STATE_LIST;
                drawList();
            }
            if (M5Cardputer.Keyboard.isKeyPressed('u') || M5Cardputer.Keyboard.isKeyPressed('U')) {
                typeDataUSB(credentials[selectedIndex].user, "Kullanici");
            }
            if (M5Cardputer.Keyboard.isKeyPressed('p') || M5Cardputer.Keyboard.isKeyPressed('P')) {
                typeDataUSB(credentials[selectedIndex].pass, "Sifre");
            }
            if (M5Cardputer.Keyboard.isKeyPressed('q') || M5Cardputer.Keyboard.isKeyPressed('Q')) {
                currentState = STATE_QR;
                String qrPayload = "User: " + credentials[selectedIndex].user + "\nPass: " + credentials[selectedIndex].pass;
                drawQR(qrPayload);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('g') || M5Cardputer.Keyboard.isKeyPressed('G') ||
                M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
                loginBuffer = "";
                currentState = STATE_VERIFY_REVEAL;
                drawVerifyScreen();
            }
        }
        else if (currentState == STATE_VERIFY_REVEAL) {
            if (status.enter) {
                if (loginBuffer == masterPassword) {
                    isPasswordRevealed = true;
                    currentState = STATE_DETAIL;
                    drawDetail();
                } else {
                    loginBuffer = "";
                    drawVerifyScreen(true);
                }
            } else if (status.del) {
                if (loginBuffer.length() > 0) {
                    loginBuffer.remove(loginBuffer.length() - 1);
                    drawVerifyScreen();
                } else {
                    currentState = STATE_DETAIL;
                    drawDetail();
                }
            } else {
                for (char c : status.word) loginBuffer += c;
                drawVerifyScreen();
            }
        }
        else if (currentState == STATE_QR) {
            if (status.del || status.enter || status.space || M5Cardputer.Keyboard.isKeyPressed('q') || M5Cardputer.Keyboard.isKeyPressed('Q')) {
                isPasswordRevealed = false;
                currentState = STATE_DETAIL;
                drawDetail();
            }
        }
    }
}