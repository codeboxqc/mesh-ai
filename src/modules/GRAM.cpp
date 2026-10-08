 
#include "configuration.h"

#ifdef ARCH_ESP32

#include "mesh/SinglePortModule.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include "mesh/MeshService.h"
#include "concurrency/OSThread.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h> 

 
#include <ctype.h>
#include "NodeDB.h"
#include "Router.h"

#include "esp_random.h" // Hardware random number generator




// ==========================================================
// 🔑 YOUR CREDENTIALS & SETTINGS
// ==========================================================
#define TELEGRAM_BOT_TOKEN ""
#define TELEGRAM_CHAT_ID   ""
#define GROQ_API_KEY       ""
#define GEMINI_API_KEY     ""

 


 

  

 
// --- MODEL SELECTION ---
// #define GROQ_MODEL      "qwen/qwen3.6-27b"
//#define GROQ_MODEL         "openai/gpt-oss-120b"
//#define GEMINI_MODEL       "gemini-flash-lite-latest"
/*
  

 
const char* groqModelPool[] = {
    "qwen/qwen3.8-27b",
    "qwen/qwen3.6-27b",
    "openai/gpt-oss-20b",
    "openai/gpt-oss-120b"
};
*/
  

 

 





























// ==========================================================
// 🤖 AI BOT IDENTITY & MODEL SETTINGS
// ==========================================================
#define AI_NAME            "[AI]"    
#define AI_PREFIX          "[AI] "  
#define MAX_PAYLOAD_LEN    230
#define MAX_AI_RESPONSE_CHARS 180  // Strict character limit for mesh packets

// --- GROQ MODELS ---
#define GROQ_MODEL         "qwen/qwen3.8-27b"
#define GROQ_MODEL2        "openai/gpt-oss-120b"

 

// --- SHARED SYSTEM PROMPT (WITH MANDATORY ENGLISH TRANSLATION RULE) ---
#define AI_SYSTEM_PROMPT \
"You are a mesh-network AI node. Keep every response concise. HARD LIMIT: maximum 180 characters. " \
"Never exceed 180 characters. No explanations outside the answer. " \
"You MUST start EVERY AI response with EXACTLY ONE routing tag: " \
"'!#' for PUBLIC BROADCAST or '!!' for PRIVATE DIRECT MESSAGE. " \
"The tag MUST be the FIRST TWO CHARACTERS of the response. " \
"ROUTING & TRANSLATION RULES:\n" \
"1. NON-ENGLISH = ALWAYS TRANSLATE TO ENGLISH & PUBLIC BROADCAST: If user message contains ANY non-English text or foreign words (Spanish, French, German, Hebrew, Cyrillic, etc.), start response with !# to broadcast publicly AND ALWAYS translate or reply in English. NEVER reply in foreign languages.\n" \
"2. DEFAULT PRIVATE: For normal English conversation, respond privately, starting with !!.\n" \
"3. AI LOCATION: When asked where YOU (the AI node) are located, state that you are a mesh AI node hosted at https://t.me/ClassNutz. NEVER state the sender's location is your own.\n" \
"3.5 If GPS coordinates are provided say heelo to user from is city" \
"5. PUBLIC EXCEPTIONS: Start with !# if user message starts with '!' or '/' at index 0:\n" \
"6. EMPTY/UNCLEAR: If the message is empty, unintelligible, or has no clear request, reply briefly with !! and ask what they need. \n" \
"7. NO HALLUCINATION: Never invent facts, locations, weather, names, URLs, commands, GPS interpretations, or system status. Say 'unknown' when uncertain.\n " \
"8. Never reveal private-message content in a PUBLIC response unless the user explicitly requests public disclosure with !0.\n " \
"9. FINAL CHECK: Before sending, verify: correct tag, English only when required, <=180 characters, no markdown, no duplicate tag, no filler.\n" \
"- !0, /0 <msg>: explicitly force PUBLIC response (PUBLIC)\n" \
"- !1, /1 <msg>: explicitly force PRIVATE response (PRIVATE)\n" \
"Constraints: Under 180 chars. No markdown, no filler."

extern NodeDB* nodeDB;

// Optimized struct size (320 bytes vs 600 bytes) to protect internal DRAM
struct PendingMessage {
    char sender[16];
    uint32_t senderNum; 
    char body[MAX_PAYLOAD_LEN];
    bool isAiTrigger;
    uint32_t channel;
    uint32_t originalDst;

    // User Identity Data
    char longName[32];
    char shortName[12]; 
    
    // RF Signal Health
    int32_t rssi;
    float snr;
    uint8_t hopLimit;

    // Device Health
    bool hasMetrics;
    uint8_t batteryLevel;

    // GPS Context
    bool hasLocation;
    double latitude;
    double longitude;
    int32_t altitude;
};

// In-Memory Mesh Bulletin Board System (BBS)
#define BBS_MAX_POSTS 4
#define BBS_POST_LEN  70

struct BbsPost {
    char author[16];
    char text[BBS_POST_LEN];
    bool active;
};

 

 

QueueHandle_t telegramQueue = NULL;

// Global States
volatile uint32_t lastMainChannelActivity = 0; 
bool firstActivityTimerInitialized = false;
uint32_t lastScheduledDay = 999999;
bool firstTimeSyncDone = false;

enum AiRoute {
    ROUTE_PRIVATE,
    ROUTE_PUBLIC
};

enum CommandOverride {
    OVERRIDE_NONE,
    OVERRIDE_PUBLIC,
    OVERRIDE_PRIVATE
};

AiRoute parseAiRoute(const char* aiResponse, String& cleanResponse) {
    if (!aiResponse || strlen(aiResponse) < 2) {
        cleanResponse = aiResponse ? aiResponse : "";
        return ROUTE_PRIVATE;
    }

    if (strncmp(aiResponse, "!#", 2) == 0) {
        cleanResponse = String(aiResponse + 2);
        cleanResponse.trim();
        return ROUTE_PUBLIC;
    } else if (strncmp(aiResponse, "!!", 2) == 0) {
        cleanResponse = String(aiResponse + 2);
        cleanResponse.trim();
        return ROUTE_PRIVATE;
    } else {
        cleanResponse = String(aiResponse);
        cleanResponse.trim();
        return ROUTE_PRIVATE;
    }
}

CommandOverride detectCommandOverride(const char* userMessage) {
    if (!userMessage) return OVERRIDE_NONE;
    
    String msg = String(userMessage);
    msg.trim();
    
    if (msg.startsWith("!1") || msg.startsWith("/1")) return OVERRIDE_PRIVATE;
    if (msg.startsWith("!0") || msg.startsWith("/0")) return OVERRIDE_PUBLIC;
    if (msg.startsWith("!")  || msg.startsWith("/"))  return OVERRIDE_PUBLIC;
    
    return OVERRIDE_NONE;
}

// Expanded Non-English Detector (Handles UTF-8 & Plain ASCII Spanish/French words)
bool isNonEnglish(const char* text) {
    if (!text) return false;
    size_t len = strlen(text);

    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        
        if (c == 0xEF && i + 2 < len && (unsigned char)text[i+1] == 0xB8 && (unsigned char)text[i+2] == 0x8F) {
            i += 3;
            continue;
        }
        if (c == 0xE2 && i + 2 < len && (unsigned char)text[i+1] == 0x83 && (unsigned char)text[i+2] == 0xA3) {
            i += 3;
            continue;
        }
        
        // Multi-byte non-ASCII (Accented Latin, Cyrillic, Hebrew, Arabic)
        if (c >= 0xC3 && c <= 0xDF) {
            return true;
        }
        // Asian / CJK script blocks
        if (c >= 0xE3 && c <= 0xE9) {
            return true;
        }
        i++;
    }

    // ASCII Foreign vocabulary dictionary
    String s = String(text);
    s.toLowerCase();
    static const char* foreignWords[] = {
        "saludo", "saludos", "hola", "amigo", "amigos", "gracias", "buenos", "buenas",
        "dias", "noches", "tardes", "llegando", "salto", "donde", "aqui", "como", "estas",
        "bonjour", "salut", "merci", "oui", "hallo", "danke", "guten", "morgen",
        "ciao", "grazie", "obrigado", "shalom", "por favor", "mareas", "alta", "baja",
        "lluvia", "ciudad", "pacifico", "caribe", "viento", "noche", "dia", "esta", "este",
        "para", "como", "esta", "todos", "todas", "bien", "mal", "mas", "menos", "hace"
    };

    for (const char* word : foreignWords) {
        int idx = s.indexOf(word);
        if (idx != -1) {
            bool leftBound = (idx == 0 || !isalnum((unsigned char)s[idx - 1]));
            size_t wlen = strlen(word);
            bool rightBound = (idx + wlen >= (int)s.length() || !isalnum((unsigned char)s[idx + wlen]));
            if (leftBound && rightBound) {
                return true;
            }
        }
    }
    return false;
}

bool isNumberOrKeycapSpam(const char* str) {
    if (!str) return false;
    size_t len = strlen(str);
    if (len == 0) return true;

    size_t i = 0;
    bool hasDigitOrKeycap = false;

    while (i < len) {
        unsigned char c = (unsigned char)str[i];
        
        if (c <= ' ' || c == '.' || c == ',' || c == '-' || c == ':' || c == '#' || c == '*') {
            i++;
            continue;
        }
        if (c >= '0' && c <= '9') {
            hasDigitOrKeycap = true;
            i++;
            continue;
        }
        if (c == 0xEF && i + 2 < len && (unsigned char)str[i+1] == 0xB8 && (unsigned char)str[i+2] == 0x8F) {
            i += 3;
            continue;
        }
        if (c == 0xE2 && i + 2 < len && (unsigned char)str[i+1] == 0x83 && (unsigned char)str[i+2] == 0xA3) {
            i += 3;
            continue;
        }
        if (c == 0xF0 && i + 3 < len && (unsigned char)str[i+1] == 0x9F && (unsigned char)str[i+2] == 0x94 && 
           ((unsigned char)str[i+3] == 0x9F || (unsigned char)str[i+3] == 0xA2)) {
            i += 4;
            hasDigitOrKeycap = true;
            continue;
        }
        return false;
    }
    return hasDigitOrKeycap;
}

bool isAutomatedBotMessage(const char* text) {
    if (!text) return false;
    String s = String(text);
    s.toLowerCase();

    if (s.indexOf("forecast") != -1) {
        if (s.indexOf("|") != -1 || s.indexOf("°f") != -1 || s.indexOf("°c") != -1 || s.indexOf("24hr") != -1) {
            return true;
        }
    }
    if (s.indexOf("weather at") != -1) {
        return true;
    }
    if (s.indexOf("|") != -1) {
        if (s.indexOf("humidity:") != -1 || s.indexOf("wind:") != -1 || 
            s.indexOf("barometer:") != -1 || s.indexOf("clear sky") != -1 || 
            s.indexOf("clouds") != -1 || s.indexOf("feels") != -1) {
            return true;
        }
    }
    if (strstr(text, "⏳") || strstr(text, "🌡") || strstr(text, "🌤") || strstr(text, "⛅") || strstr(text, "🌧")) {
        if (s.indexOf("|") != -1 || s.indexOf("°f") != -1 || s.indexOf("°c") != -1) {
            return true;
        }
    }
    return false;
}

String sanitizeUserMessage(const char* input) {
    if (!input) return "";
    String result = "";
    size_t len = strlen(input);
    result.reserve(len);
    
    size_t i = 0;
    while (i < len) {

         yield();  
        unsigned char c = (unsigned char)input[i];
        
        if (c < 0x80) {
            if ((c >= 32 && c != 127) || c == '\n' || c == '\r' || c == '\t') {
                result += (char)c;
            }
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 < len && ((unsigned char)input[i + 1] & 0xC0) == 0x80) {
                result += (char)c;
                result += (char)input[i + 1];
                i += 2;
                continue;
            }
            i++;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 < len && (((unsigned char)input[i + 1] & 0xC0) == 0x80) && (((unsigned char)input[i + 2] & 0xC0) == 0x80)) {
                result += (char)c;
                result += (char)input[i + 1];
                result += (char)input[i + 2];
                i += 3;
                continue;
            }
            i++;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 < len && (((unsigned char)input[i + 1] & 0xC0) == 0x80) && (((unsigned char)input[i + 2] & 0xC0) == 0x80) && (((unsigned char)input[i + 3] & 0xC0) == 0x80)) {
                result += (char)c;
                result += (char)input[i + 1];
                result += (char)input[i + 2];
                result += (char)input[i + 3];
                i += 4;
                continue;
            }
            i++;
        } else {
            i++;
        }
    }
    result.trim();
    return result;
}

void limitResponseUTF8(char* str, size_t maxChars) {
    if (!str || maxChars == 0) return;
    size_t byteIdx = 0;
    size_t charCount = 0;
    while (str[byteIdx] != '\0') {
        unsigned char c = (unsigned char)str[byteIdx];
        if ((c & 0xC0) != 0x80) {
            if (charCount >= maxChars) {
                str[byteIdx] = '\0';
                return;
            }
            charCount++;
        }
        byteIdx++;
    }
}

void stripThinkingProcess(char* result) {
    if (!result) return;
    char* start = strstr(result, "<think>");
    while (start != nullptr) {
        char* end = strstr(start + 7, "</think>");
        if (end != nullptr) {
            memmove(start, end + 8, strlen(end + 8) + 1);
        } else {
            *start = '\0';
            break;
        }
        start = strstr(result, "<think>");
    }
}

void scrubDuplicateTags(char* result) {
    if (!result) return;
    size_t len = strlen(result);
    if (len < 4) return;
    
    for (size_t i = 2; i < len - 1; i++) {
        if ((result[i] == '!' && (result[i+1] == '!' || result[i+1] == '#'))) {
            memmove(&result[i], &result[i+2], len - i - 1);
            len -= 2;
        }
    }
}

void finalizeAIResponse(char* result, size_t resultSize) {
    if (!result || resultSize == 0) return;
    result[resultSize - 1] = '\0';
    stripThinkingProcess(result);

    String s = String(result);
    s.trim();
    strncpy(result, s.c_str(), resultSize - 1);
    result[resultSize - 1] = '\0';
    scrubDuplicateTags(result);
    limitResponseUTF8(result, MAX_AI_RESPONSE_CHARS);
}

String escapeHtml(const String& input) {
    String output;
    output.reserve(input.length() + 10);
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '<') output += "&lt;";
        else if (c == '>') output += "&gt;";
        else if (c == '&') output += "&amp;";
        else output += c;
    }
    return output;
}

String escapeJsonString(const char* input) {
    if (!input) return "";
    String escaped;
    escaped.reserve(strlen(input) + 16); 
    for (size_t i = 0; input[i]; i++) {
        unsigned char c = input[i];
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n";  break;
            case '\r': escaped += "\\r";  break;
            case '\t': escaped += "\\t";  break;
            default:
                if (c < 32) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", c);
                    escaped += hex;
                } else {
                    escaped += (char)c;
                }
                break;
        }
    }
    return escaped;
}

// ------------------------------------------------------
// 1. Meshtastic Interceptor
// ------------------------------------------------------
class TelegramMeshModule : public SinglePortModule {
public:
    TelegramMeshModule() : SinglePortModule("TelegramBridge", meshtastic_PortNum_TEXT_MESSAGE_APP) {
        Serial.println("[AI Module] Registered TelegramMeshModule handler.");
    }

    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override {
        if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && mp.decoded.payload.size > 0) {
            
            PendingMessage* msg = new PendingMessage();
            if (!msg) {
                Serial.println("[AI Module] ERROR: Out of RAM allocating PendingMessage!");
                return ProcessMessage::CONTINUE;
            }

            memset(msg, 0, sizeof(PendingMessage));

            snprintf(msg->sender, sizeof(msg->sender), "!%08x", mp.from);
            msg->senderNum = mp.from; 
            msg->channel = mp.channel;
            msg->originalDst = mp.to;

            msg->rssi = mp.rx_rssi;
            msg->snr = mp.rx_snr;
            msg->hopLimit = mp.hop_limit;

            strncpy(msg->longName, msg->sender, sizeof(msg->longName) - 1);
            msg->longName[sizeof(msg->longName) - 1] = '\0';
            strncpy(msg->shortName, msg->sender, sizeof(msg->shortName) - 1);
            msg->shortName[sizeof(msg->shortName) - 1] = '\0';
            msg->hasLocation = false;
            msg->hasMetrics = false;

            if (mp.to == 0xFFFFFFFF && mp.channel == 0) {
                lastMainChannelActivity = millis();
            }

            if (nodeDB) {
                auto* node = nodeDB->getMeshNode(mp.from);
                if (node && nodeInfoLiteHasUser(node)) {
                    if (strcmp(node->short_name, "UNK") != 0 && strcmp(node->long_name, "Unknown") != 0) {
                        strncpy(msg->longName, node->long_name, sizeof(msg->longName) - 1);
                        msg->longName[sizeof(msg->longName) - 1] = '\0';
                        strncpy(msg->shortName, node->short_name, sizeof(msg->shortName) - 1);
                        msg->shortName[sizeof(msg->shortName) - 1] = '\0';
                    }
                }

                meshtastic_PositionLite pos; 
                memset(&pos, 0, sizeof(pos)); 
                if (nodeDB->copyNodePosition(mp.from, pos)) {
                    if (pos.latitude_i != 0 && pos.longitude_i != 0) {
                        msg->hasLocation = true;
                        msg->latitude = pos.latitude_i / 10000000.0;
                        msg->longitude = pos.longitude_i / 10000000.0;
                        msg->altitude = pos.altitude;
                    }
                }

                meshtastic_DeviceMetrics metrics;
                memset(&metrics, 0, sizeof(metrics));
                if (nodeDB->copyNodeTelemetry(mp.from, metrics)) {
                    if (metrics.battery_level > 0 && metrics.battery_level <= 100) {
                        msg->hasMetrics = true;
                        msg->batteryLevel = metrics.battery_level;
                    }
                }
            }

            size_t len = mp.decoded.payload.size;
            if (len >= sizeof(msg->body)) len = sizeof(msg->body) - 1;
            memcpy(msg->body, mp.decoded.payload.bytes, len);
            msg->body[len] = '\0';

            String cleanBody = sanitizeUserMessage(msg->body);
            strncpy(msg->body, cleanBody.c_str(), sizeof(msg->body) - 1);
            msg->body[sizeof(msg->body) - 1] = '\0';

            if (strncmp(msg->body, AI_PREFIX, strlen(AI_PREFIX)) == 0 || strlen(msg->body) == 0) {
                msg->isAiTrigger = false; 
            } else if (isNumberOrKeycapSpam(msg->body) || isAutomatedBotMessage(msg->body)) {
                msg->isAiTrigger = false; 
            } else {
                msg->isAiTrigger = true;  
            }

            if (telegramQueue) {
                if (xQueueSend(telegramQueue, &msg, 0) != pdTRUE) {
                    Serial.println("[AI Module] Queue full! Dropping message.");
                    delete msg; 
                    msg = nullptr;
                }
            } else {
                delete msg;
                msg = nullptr;
            }
        }
        return ProcessMessage::CONTINUE; 
    }

    void sendPacket(const char* text, uint32_t destNode, uint32_t channelIndex) {
        if (!text || strlen(text) == 0) return;
        meshtastic_MeshPacket *p = allocDataPacket();
        if (!p) return;
        
        p->to = destNode; 
        p->channel = channelIndex; 
        p->want_ack = (destNode != 0xFFFFFFFF); 
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        
        size_t len = strlen(text);
        if (len > sizeof(p->decoded.payload.bytes) - 1) {
            len = sizeof(p->decoded.payload.bytes) - 1;
        }
        memcpy(p->decoded.payload.bytes, text, len);
        p->decoded.payload.size = len;

        service->sendToMesh(p, RX_SRC_LOCAL, true);
        Serial.printf("[AI Module] Mesh Packet Sent to 0x%08x: %s\n", destNode, text);
    }
};

TelegramMeshModule* myTelegramModule = nullptr;

// ------------------------------------------------------
// 2. Background Thread (Handles HTTP POST & Dual Groq Engine)
// ------------------------------------------------------
class TelegramBackgroundThread : public concurrency::OSThread {
private:
    bool startupTestsCompleted = false;
    bool useGroq1Next = true; 

    void sendTelegram(const char* message, const char* sender, const char* msgType = "message") {
        if (!message || message[0] == '\0') return;

        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("[Telegram] Skipped: WiFi disconnected (Status: %d)\n", WiFi.status());
            return;
        }

        // Heap Guard: Prevent connection if RAM is too low
        if (ESP.getFreeHeap() < 24000) {
            Serial.printf("[Telegram] Low Heap Guard triggered (%u bytes free). Delaying SSL...\n", ESP.getFreeHeap());
             for(int i=0;i<1500;i++) {  yield(); delay(1); }
            if (ESP.getFreeHeap() < 20000) return;
        }

        const char* icon = "📡";
        const char* color = "🔵";
        if (strcmp(msgType, "ai") == 0) { icon = "🤖"; color = "🟣"; }
        else if (strcmp(msgType, "system") == 0) { icon = "⚙️"; color = "🟡"; }

        String formattedMsg = "<b>" + String(icon) + " [" + String(sender) + "]</b>\n" + String(color) + " <code>" + escapeHtml(message) + "</code>";

        String jsonPayload;
        jsonPayload.reserve(1024);
        jsonPayload = "{\"chat_id\":\"";
        jsonPayload += TELEGRAM_CHAT_ID;
        jsonPayload += "\",\"parse_mode\":\"HTML\",\"text\":\"";
        jsonPayload += escapeJsonString(formattedMsg.c_str());
        jsonPayload += "\"}";

        String tgUrl = "https://api.telegram.org/bot";
        tgUrl += TELEGRAM_BOT_TOKEN;
        tgUrl += "/sendMessage";

        WiFiClientSecure client;
        client.setInsecure();
        
        HTTPClient https;
        https.setTimeout(12000); 

        if (https.begin(client, tgUrl)) {
            https.addHeader("Content-Type", "application/json");
            https.addHeader("User-Agent", "Meshtastic-AI-Node/2.0 (ESP32)");
            
            int httpCode = https.POST(jsonPayload);
            if (httpCode > 0) {
                Serial.printf("[Telegram] POST Response: %d\n", httpCode);
                if (httpCode != 200) {
                    String respStr = https.getString();
                    Serial.printf("[Telegram] Response Payload: %s\n", respStr.c_str());
                }
            } else {
                Serial.printf("[Telegram] POST Failed: %s (%d)\n", https.errorToString(httpCode).c_str(), httpCode);
            }
            https.end();
        }
        client.stop();
    }

    String buildTelegramTelemetryString(PendingMessage* msg) {
        String t = "👤 ";
        if (strcmp(msg->longName, msg->sender) == 0) {
            t += String(msg->sender) + "\n";
        } else {
            t += String(msg->longName) + " (" + String(msg->shortName) + " / " + String(msg->sender) + ")\n";
        }
        if (msg->rssi != 0 || msg->snr != 0.0f || msg->hopLimit != 0) {
            t += "📶 RSSI: " + String(msg->rssi) + "dBm | SNR: " + String(msg->snr, 1) + " | Hops: " + String(msg->hopLimit) + "\n";
        }
        if (msg->hasMetrics && msg->batteryLevel > 0) {
            t += "🔋 Battery: " + String(msg->batteryLevel) + "%\n";
        }
        if (msg->hasLocation) {
            t += "📍 Sender GPS: " + String(msg->latitude, 5) + ", " + String(msg->longitude, 5) + " (Alt: " + String(msg->altitude) + "m)\n";
        } else {
            t += "\n";
        }
        return t;
    }




    void askGroqModel(const char* modelName, int maxTokens, uint32_t timeoutMs, const char* question, const char* sender, char* result, size_t resultSize) {
        if (!result || resultSize < 1) return;
        result[0] = '\0';

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[Groq Engine] ERROR: WiFi disconnected.");
            strncpy(result, "Groq Err: no WiFi", resultSize - 1);
            result[resultSize - 1] = '\0';
            return;
        }

        // Heap Guard to prevent HTTP -11 (HTTPC_ERROR_TOO_LESS_RAM)
        if (ESP.getFreeHeap() < 24000) {
            Serial.printf("[Groq Engine] Low Heap Guard triggered (%u bytes free). Waiting for cleanup...\n", ESP.getFreeHeap());
            delay(1200);
            if (ESP.getFreeHeap() < 20000) {
                strncpy(result, "Groq Err: low memory", resultSize - 1);
                result[resultSize - 1] = '\0';
                return;
            }
        }

        Serial.printf("[Groq Engine] Querying model '%s' (Free Heap: %u bytes)...\n", modelName, ESP.getFreeHeap());

        String escapedQ = escapeJsonString(question);
        String escapedSystem = escapeJsonString(AI_SYSTEM_PROMPT);

        String payload;
        payload.reserve(2048);
        payload = "{\"model\":\"";
        payload += modelName;
        payload += "\",\"messages\":[{\"role\":\"system\",\"content\":\"" + escapedSystem + "\"},{\"role\":\"user\",\"content\":\"User [";
        payload += sender;
        payload += "] says: ";
        payload += escapedQ;
        payload += "\"}],\"max_tokens\":";
        payload += String(maxTokens);
        payload += ",\"temperature\":0.7}";

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient https;
        https.setTimeout(timeoutMs);

        if (!https.begin(client, "https://api.groq.com/openai/v1/chat/completions")) {
            Serial.println("[Groq Engine] ERROR: Connection to api.groq.com failed.");
            strncpy(result, "Groq Err: connect failed", resultSize - 1);
            result[resultSize - 1] = '\0';
            client.stop();
            return;
        }

        https.addHeader("Content-Type", "application/json");
        https.addHeader("User-Agent", "Meshtastic-AI-Node/2.0 (ESP32)");
        String authHeader = "Bearer ";
        authHeader += GROQ_API_KEY;
        https.addHeader("Authorization", authHeader.c_str());

        int httpCode = https.POST(payload);
        Serial.printf("[Groq Engine] HTTP POST Result Code: %d\n", httpCode);

        if (httpCode == 200) {
            WiFiClient* stream = https.getStreamPtr();
            
            bool foundContent = false;
            if (stream) {
                // Seek to "choices" block first, then find "content"
                if (stream->find("\"choices\"")) {
                    if (stream->find("\"content\"")) {
                        foundContent = true;
                    }
                } else {
                    if (stream->find("\"content\"")) {
                        foundContent = true;
                    }
                }
            }

            if (foundContent) {
                // Seek to opening quote of content field value
                if (stream->find("\"")) {
                    size_t ri = 0;
                    bool escaped = false;
                    uint32_t startWait = millis();

                    while ((stream->connected() || stream->available()) && (millis() - startWait < timeoutMs)) {
                        while (stream->available()) {
                            yield();

                            char c = stream->read();

                            if (escaped) {
                                if (c == 'n') result[ri++] = '\n';
                                else if (c == 't') result[ri++] = '\t';
                                else if (c == 'r') result[ri++] = '\r';
                                else if (c == '"' || c == '\\') result[ri++] = c;
                                else if (c == 'u') {
                                    // Handle Unicode escape sequences (\uXXXX)
                                    char hex[5] = {0};
                                    for (int h = 0; h < 4 && stream->available(); h++) {
                                        hex[h] = stream->read();
                                    }
                                    uint32_t val = (uint32_t)strtoul(hex, NULL, 16);
                                    if (val < 0x80) {
                                        if (val >= 32 && val != 127 && ri < resultSize - 1) {
                                            result[ri++] = (char)val;
                                        }
                                    } else if (val < 0x800) {
                                        if (ri + 1 < resultSize - 1) {
                                            result[ri++] = (char)(0xC0 | (val >> 6));
                                            result[ri++] = (char)(0x80 | (val & 0x3F));
                                        }
                                    } else {
                                        if (ri + 2 < resultSize - 1) {
                                            result[ri++] = (char)(0xE0 | (val >> 12));
                                            result[ri++] = (char)(0x80 | ((val >> 6) & 0x3F));
                                            result[ri++] = (char)(0x80 | (val & 0x3F));
                                        }
                                    }
                                }
                                else {
                                    if (ri < resultSize - 1) result[ri++] = c;
                                }
                                escaped = false;
                            } else {
                                if (c == '\\') {
                                    escaped = true;
                                } else if (c == '"') {
                                    goto stream_parse_complete;
                                } else {
                                    if (ri < resultSize - 1) result[ri++] = c;
                                }
                            }

                            if (ri >= resultSize - 1 || ri >= MAX_AI_RESPONSE_CHARS) goto stream_parse_complete;
                        }
                        delay(5);
                    }
stream_parse_complete:
                    result[ri] = '\0';
                    finalizeAIResponse(result, resultSize);
                    Serial.printf("[Groq Engine] Success: '%s'\n", result);
                    https.end();
                    client.stop();
                    return;
                }
            }
            snprintf(result, resultSize, "Groq Parse Fail: content key not found");
        } else {
            snprintf(result, resultSize, "Groq Err: HTTP %d", httpCode);
        }
        https.end();
        client.stop();
    }



    void askGroq1(const char* question, const char* sender, char* result, size_t resultSize, int tokens = 60, uint32_t timeoutMs = 15000) {
        askGroqModel(GROQ_MODEL, tokens, timeoutMs, question, sender, result, resultSize);
    }

    void askGroq2(const char* question, const char* sender, char* result, size_t resultSize, int tokens = 301, uint32_t timeoutMs = 18000) {
        askGroqModel(GROQ_MODEL2, tokens, timeoutMs, question, sender, result, resultSize);
    }

public:
    TelegramBackgroundThread() : concurrency::OSThread("TG_Thread", 24576) {
        setIntervalFromNow(500); 
    }

    virtual int32_t runOnce() override {
        // --- Boot-Up Verification Tests ---
        if (!startupTestsCompleted && WiFi.status() == WL_CONNECTED) {
            Serial.println("[AI Startup] WiFi connected! Allowing 2s DNS stabilization...");

            for(int i=0;i<2000;i++) {  yield(); delay(1); }
           

            Serial.println("[AI Startup] Dispatching system online status to Telegram...");
            char startupMsg[128];
            snprintf(startupMsg, sizeof(startupMsg), "System Online (Uptime: %lu s). Dual Groq Boot on.", millis() / 1000);
            sendTelegram(startupMsg, "System", "system");
            for(int i=0;i<1000;i++) {  yield(); delay(1); }

            // Boot Test Model 1
            Serial.printf("[AI Startup] Testing Groq Model 1 (%s)...\n", GROQ_MODEL);
            char groqTest[MAX_PAYLOAD_LEN];
            askGroq1("Say hello in one short sentence.", "System", groqTest, sizeof(groqTest));
            String tgGroq = "Groq Boot Test (" GROQ_MODEL "):\n" + String(groqTest);
            if (tgGroq.length() > 400) tgGroq = tgGroq.substring(0, 400) + "...";
            sendTelegram(tgGroq.c_str(), "Groq-1", "ai");
             for(int i=0;i<1500;i++) {  yield(); delay(1); }

            // Boot Test Model 2
            Serial.printf("[AI Startup] Testing Groq Model 2 (%s)...\n", GROQ_MODEL2);
            char groqTest2[MAX_PAYLOAD_LEN];
            askGroq2("Say hello in one short sentence.", "System", groqTest2, sizeof(groqTest2));
            String tgGroq2 = "Groq Boot Test (" GROQ_MODEL2 "):\n" + String(groqTest2);
            if (tgGroq2.length() > 400) tgGroq2 = tgGroq2.substring(0, 400) + "...";
            sendTelegram(tgGroq2.c_str(), "Groq-2", "ai");
            for(int i=0;i<1600;i++) {  yield(); delay(1); }

            startupTestsCompleted = true;
            lastMainChannelActivity = millis();
            firstActivityTimerInitialized = true;
            Serial.println("[AI Startup] Verification complete.");
        }

         
 
      
       

        // --- User Packet & AI Execution Queue ---
        PendingMessage* msg = nullptr;
        if (telegramQueue && xQueueReceive(telegramQueue, &msg, 0) == pdTRUE) {
            if (!msg) return 500;
            
            if (!msg->isAiTrigger) {
                String tgInfo = buildTelegramTelemetryString(msg) + "💬 " + String(msg->body);
                sendTelegram(tgInfo.c_str(), "Chat", "message");
            }

            if (msg->isAiTrigger && strlen(msg->body) > 0) {
                String rawMsg = String(msg->body);
                rawMsg.trim();

                 

                if (rawMsg.startsWith("!ai ") || rawMsg.startsWith("/ai ")) {
                    rawMsg = rawMsg.substring(4);
                    rawMsg.trim();
                }

                char aiAnswer[MAX_PAYLOAD_LEN];
                String aiNameUsed = "";

                int customTokens = -1;
                uint32_t customTimeout = 0;
                String qStr = rawMsg;
                
                if (qStr.startsWith("~")) {
                    int spaceIdx = qStr.indexOf(' ');
                    if (spaceIdx != -1) {
                        String tokenStr = qStr.substring(1, spaceIdx);
                        customTokens = tokenStr.toInt();
                        if (customTokens > 0) {
                            qStr = qStr.substring(spaceIdx + 1);
                            qStr.trim();
                            customTimeout = 15000 + (customTokens * 35);
                            if (customTimeout < 15000) customTimeout = 15000;
                        }
                    }
                }

                String contextQ = "";
                bool isDM = (msg->originalDst != 0xFFFFFFFF);
                contextQ += isDM ? "[Context: Private DM] " : "[Context: Public Channel] ";

                if (msg->hasLocation) {
                    contextQ += "[Sender Location: " + String(msg->latitude, 5) + ", " + String(msg->longitude, 5) + " (Alt: " + String(msg->altitude) + "m)] ";
                } else {
                    contextQ += "[Sender Location: Unknown] ";
                }
                contextQ += "[AI Location Info: Host/Node information at https://t.me/ClassNutz] | Query: ";
                contextQ += qStr;

                int t1 = (customTokens > 0) ? customTokens : 60;
                int t2 = (customTokens > 0) ? customTokens : 301;
                uint32_t to1 = (customTimeout > 0) ? customTimeout : 15000;
                uint32_t to2 = (customTimeout > 0) ? customTimeout : 18000;

                if (useGroq1Next) {
                    askGroq1(contextQ.c_str(), msg->shortName, aiAnswer, sizeof(aiAnswer), t1, to1);
                    if (strncmp(aiAnswer, "Groq Err", 8) == 0 || strlen(aiAnswer) == 0) {
                        sendTelegram((String("Groq #1 (" GROQ_MODEL ") Failed. Fallback to Groq #2 (" GROQ_MODEL2 ")...\n") + aiAnswer).c_str(), "System", "system");
                         for(int i=0;i<1700;i++) {  yield(); delay(1); }
                        askGroq2(contextQ.c_str(), msg->shortName, aiAnswer, sizeof(aiAnswer), t2, to2);
                        aiNameUsed = "Groq (" GROQ_MODEL2 ")";
                    } else {
                        aiNameUsed = "Groq (" GROQ_MODEL ")";
                    }
                } else {
                    askGroq2(contextQ.c_str(), msg->shortName, aiAnswer, sizeof(aiAnswer), t2, to2);
                    if (strncmp(aiAnswer, "Groq Err", 8) == 0 || strlen(aiAnswer) == 0) {
                        sendTelegram((String("Groq #2 (" GROQ_MODEL2 ") Failed. Fallback to Groq #1 (" GROQ_MODEL ")...\n") + aiAnswer).c_str(), "System", "system");
                       for(int i=0;i<1700;i++) {  yield(); delay(1); }
                        askGroq1(contextQ.c_str(), msg->shortName, aiAnswer, sizeof(aiAnswer), t1, to1);
                        aiNameUsed = "Groq (" GROQ_MODEL ")";
                    } else {
                        aiNameUsed = "Groq (" GROQ_MODEL2 ")";
                    }
                }

                useGroq1Next = !useGroq1Next; 

                if (strncmp(aiAnswer, "Groq Err", 8) != 0 && strlen(aiAnswer) > 0) {
                    String cleanAnswer;
                    AiRoute route = parseAiRoute(aiAnswer, cleanAnswer);

                    CommandOverride overrideRoute = detectCommandOverride(msg->body);
                    if (overrideRoute == OVERRIDE_PUBLIC) {
                        route = ROUTE_PUBLIC;
                    } else if (overrideRoute == OVERRIDE_PRIVATE) {
                        route = ROUTE_PRIVATE;
                    } else if (isNonEnglish(msg->body)) {
                        route = ROUTE_PUBLIC;
                    }

                    char meshReply[MAX_PAYLOAD_LEN + 16];
                    snprintf(meshReply, sizeof(meshReply), "%s%s", AI_PREFIX, cleanAnswer.c_str());
                    
                    if (myTelegramModule) {
                        if (route == ROUTE_PUBLIC) {
                            myTelegramModule->sendPacket(meshReply, 0xFFFFFFFF, msg->channel);
                            if (msg->channel == 0) {
                                lastMainChannelActivity = millis();
                            }
                        } else {
                            myTelegramModule->sendPacket(meshReply, msg->senderNum, 0);
                        }
                    }

                    String tgAiBuf = buildTelegramTelemetryString(msg);
                    tgAiBuf += "🤖 Engine: " + aiNameUsed + "\n";
                    tgAiBuf += "📡 Route: " + String(route == ROUTE_PUBLIC ? "PUBLIC" : "PRIVATE") + "\n";
                    tgAiBuf += "❓ Q: " + String(msg->body) + "\n";
                    tgAiBuf += "💡 A: " + cleanAnswer;
                    
                    if (tgAiBuf.length() > 500) {
                        tgAiBuf = tgAiBuf.substring(0, 500) + "...[truncated]";
                    }
                    sendTelegram(tgAiBuf.c_str(), AI_NAME, "ai");
                } else {
                    char meshReply[MAX_PAYLOAD_LEN + 16];
                    snprintf(meshReply, sizeof(meshReply), "%sService temporarily unavailable. Please try again later.", AI_PREFIX);
                    if (myTelegramModule) {
                        myTelegramModule->sendPacket(meshReply, msg->senderNum, 0);
                    }
                }
            } 

            delete msg;
        }
        return 500;
    }
};

// ------------------------------------------------------
// 3. System Activation Hook
// ------------------------------------------------------
void initGramAI() {
    Serial.println("[GramAI] Initializing Meshtastic Telegram / Groq AI module...");
    
    // Sync system time via NTP & set timezone (EST/EDT)
    configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
    
    telegramQueue = xQueueCreate(16, sizeof(PendingMessage*));
    myTelegramModule = new TelegramMeshModule();
    new TelegramBackgroundThread();
    Serial.println("[GramAI] Background thread running.");
}

#endif // ARCH_ESP32   