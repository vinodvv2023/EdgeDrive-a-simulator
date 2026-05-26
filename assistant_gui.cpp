#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <iostream>
#include <chrono>
#include <map>
#include <cfloat>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "httplib.h"
#include "sherpa-onnx/c-api/c-api.h"

// Forward declarations for ImGui / DirectX
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Application State
enum AssistantState {
    STATE_IDLE,
    STATE_LISTENING,
    STATE_PROCESSING_STT,
    STATE_PROCESSING_LLM,
    STATE_SPEAKING
};

AssistantState current_state = STATE_IDLE;
std::string status_message = "Initializing...";
std::string transcript_text = "";
std::string llm_response_text = "";
std::string display_response_text = ""; 

// ─── Orchestrator-Driven Dynamic Telemetry Config ────────────────────────
enum class FieldType { PROGRESS_BAR, TEXT, BOOLEAN, GRID_4 };

struct FieldDef {
    std::string              key;
    std::string              label;
    std::string              unit;
    FieldType                type        = FieldType::TEXT;
    float                    max_val     = 100.0f;
    float                    warn_above  = FLT_MAX;
    float                    warn_below  = -FLT_MAX;
    std::string              warn_label;
    std::string              true_label  = "YES";
    std::string              false_label = "NO";
    std::vector<std::string> sublabels;
    bool                     llm_context = true;
};

std::vector<FieldDef>                      g_field_defs;    // populated once from /api/telemetry_config
std::map<std::string, double>              g_values;        // scalar live values
std::map<std::string, std::vector<double>> g_array_values;  // array live values (e.g. tyre_pressures)
std::mutex   telemetry_mutex;
bool         telemetry_thread_running = true;

// ─── JSON Parse Helpers ───────────────────────────────────────────────────
bool parse_json_bool(const std::string& str, size_t pos) {
    std::string sub = str.substr(pos, 15);
    return sub.find("true") != std::string::npos;
}

float parse_json_float(const std::string& str, size_t pos) {
    size_t i = pos + 1;
    while (i < str.size() && (str[i] == ' ' || str[i] == ':')) i++;
    size_t end = str.find_first_of(",}]", i);
    if (end == std::string::npos) return 0.0f;
    try { return std::stof(str.substr(i, end - i)); } catch (...) { return 0.0f; }
}

int parse_json_int(const std::string& str, size_t pos) {
    size_t i = pos + 1;
    while (i < str.size() && (str[i] == ' ' || str[i] == ':')) i++;
    size_t end = str.find_first_of(",}]", i);
    if (end == std::string::npos) return 0;
    try { return std::stoi(str.substr(i, end - i)); } catch (...) { return 0; }
}

// Extract a quoted string value: { "key": "value" }
std::string parse_json_string_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    size_t colon = json.find(":", pos);
    if (colon == std::string::npos) return "";
    size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

// Extract an array of quoted strings: "key": ["a", "b"]
std::vector<std::string> parse_json_string_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return result;
    size_t bracket = json.find('[', pos);
    if (bracket == std::string::npos) return result;
    size_t close  = json.find(']', bracket);
    if (close == std::string::npos) return result;
    std::string arr = json.substr(bracket + 1, close - bracket - 1);
    size_t i = 0;
    while ((i = arr.find('"', i)) != std::string::npos) {
        size_t j = arr.find('"', i + 1);
        if (j == std::string::npos) break;
        result.push_back(arr.substr(i + 1, j - i - 1));
        i = j + 1;
    }
    return result;
}

// Split a JSON string into top-level { } objects
std::vector<std::string> split_json_objects(const std::string& json) {
    std::vector<std::string> objects;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < json.size(); i++) {
        if (json[i] == '{') { if (depth == 0) start = i; depth++; }
        else if (json[i] == '}') {
            depth--;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(json.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

// ─── Fetch Telemetry Config (once at startup) ───────────────────────────
void fetch_telemetry_config() {
    try {
        httplib::Client client("127.0.0.1", 8082);
        client.set_connection_timeout(3, 0);
        client.set_read_timeout(3, 0);
        auto res = client.Get("/api/telemetry_config");
        if (!res || res->status != 200) {
            std::cerr << "[Config] Could not fetch /api/telemetry_config. Dashboard will be empty until orchestrator is running." << std::endl;
            return;
        }
        std::string body = res->body;
        size_t fields_pos = body.find("\"fields\"");
        if (fields_pos == std::string::npos) return;
        size_t arr_start = body.find('[', fields_pos);
        if (arr_start == std::string::npos) return;

        auto field_objects = split_json_objects(body.substr(arr_start));

        std::lock_guard<std::mutex> lock(telemetry_mutex);
        g_field_defs.clear();
        for (const auto& obj : field_objects) {
            FieldDef fd;
            fd.key = parse_json_string_value(obj, "key");
            if (fd.key.empty()) continue;
            fd.label       = parse_json_string_value(obj, "label");
            fd.unit        = parse_json_string_value(obj, "unit");
            fd.warn_label  = parse_json_string_value(obj, "warn_label");
            fd.true_label  = parse_json_string_value(obj, "true_label");
            fd.false_label = parse_json_string_value(obj, "false_label");
            fd.sublabels   = parse_json_string_array(obj, "sublabels");
            if (fd.true_label.empty())  fd.true_label  = "YES";
            if (fd.false_label.empty()) fd.false_label = "NO";

            std::string t = parse_json_string_value(obj, "type");
            if      (t == "progress_bar") fd.type = FieldType::PROGRESS_BAR;
            else if (t == "boolean")      fd.type = FieldType::BOOLEAN;
            else if (t == "grid_4")       fd.type = FieldType::GRID_4;
            else                          fd.type = FieldType::TEXT;

            size_t p;
            if ((p = obj.find("\"max\""))        != std::string::npos) fd.max_val    = parse_json_float(obj, obj.find(':', p));
            if ((p = obj.find("\"warn_above\"")) != std::string::npos) fd.warn_above = parse_json_float(obj, obj.find(':', p));
            if ((p = obj.find("\"warn_below\"")) != std::string::npos) fd.warn_below = parse_json_float(obj, obj.find(':', p));
            if ((p = obj.find("\"llm_context\""))!= std::string::npos) fd.llm_context = parse_json_bool(obj, obj.find(':', p));
            else fd.llm_context = true;

            g_field_defs.push_back(fd);
        }
        std::cout << "[Config] Loaded " << g_field_defs.size() << " telemetry fields from orchestrator." << std::endl;
    } catch (...) {
        std::cerr << "[Config] Exception while fetching telemetry config." << std::endl;
    }
}

// ─── Background Telemetry Fetch Worker (10Hz, generic) ──────────────────
void telemetry_fetch_worker() {
    while (telemetry_thread_running) {
        try {
            httplib::Client client("127.0.0.1", 8082);
            client.set_connection_timeout(0, 500000);
            client.set_read_timeout(0, 500000);
            auto res = client.Get("/api/vehicle_status");
            if (res && res->status == 200) {
                std::string body = res->body;
                static int log_counter = 0;
                if (++log_counter % 50 == 0)
                    std::cout << "[Telemetry] " << body << std::endl;

                std::map<std::string, double>              new_vals;
                std::map<std::string, std::vector<double>> new_arr_vals;

                // Copy field defs under lock, then parse without holding the lock
                std::vector<FieldDef> fields_copy;
                { std::lock_guard<std::mutex> lk(telemetry_mutex); fields_copy = g_field_defs; }

                for (const auto& field : fields_copy) {
                    std::string search = "\"" + field.key + "\"";
                    size_t pos = body.find(search);
                    if (pos == std::string::npos) continue;
                    pos = body.find(':', pos);
                    if (pos == std::string::npos) continue;

                    if (field.type == FieldType::GRID_4) {
                        size_t as = body.find('[', pos);
                        if (as == std::string::npos) continue;
                        size_t ae = body.find(']', as);
                        if (ae == std::string::npos) continue;
                        std::string arr_str = body.substr(as + 1, ae - as - 1);
                        std::vector<double> vals;
                        size_t i = 0;
                        while (i < arr_str.size()) {
                            while (i < arr_str.size() && (arr_str[i]==' '||arr_str[i]==',')) i++;
                            if (i >= arr_str.size()) break;
                            try { size_t n; vals.push_back(std::stod(arr_str.substr(i), &n)); i += n; }
                            catch (...) { break; }
                        }
                        new_arr_vals[field.key] = vals;
                    } else if (field.type == FieldType::BOOLEAN) {
                        new_vals[field.key] = parse_json_bool(body, pos) ? 1.0 : 0.0;
                    } else {
                        new_vals[field.key] = (double)parse_json_float(body, pos);
                    }
                }

                std::lock_guard<std::mutex> lk(telemetry_mutex);
                g_values       = new_vals;
                g_array_values = new_arr_vals;
            }

        } catch (...) {
            static int exc = 0;
            if (++exc % 50 == 0)
                std::cerr << "[Telemetry] Exception in fetch worker." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Audio recording buffer
std::vector<float> audio_buffer;
std::mutex audio_mutex;

// Silence detection globals
float silence_duration = 0.0f;
float speech_duration = 0.0f;
const float SILENCE_THRESHOLD = 0.005f; // Threshold for amplitude

// Conversation history
std::vector<std::pair<std::string, std::string>> chat_history;

// Helper to unescape JSON strings for the GUI display
std::string unescape_json_string(const std::string& input) {
    std::string out;
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '\\' && i + 1 < input.length()) {
            if (input[i+1] == 'n') { out += '\n'; i++; continue; }
            if (input[i+1] == 't') { out += '\t'; i++; continue; }
            if (input[i+1] == 'r') { out += '\r'; i++; continue; }
            if (input[i+1] == '"') { out += '"'; i++; continue; }
            if (input[i+1] == '\\') { out += '\\'; i++; continue; }
        }
        out += input[i];
    }
    return out;
}

// Helper to sanitize strings for JSON payloads
std::string clean_for_json(std::string str) {
    size_t pos = 0;
    while ((pos = str.find("\\", pos)) != std::string::npos) {
        str.replace(pos, 1, "/");
        pos += 1;
    }
    pos = 0;
    while ((pos = str.find("\"", pos)) != std::string::npos) {
        str.replace(pos, 1, "'");
        pos += 1;
    }
    pos = 0;
    while ((pos = str.find("\n", pos)) != std::string::npos) {
        str.replace(pos, 1, " ");
        pos += 1;
    }
    return str;
}

// Helper to asynchronously report voice assistant events to the orchestrator
void report_voice_state(const std::string& state, const std::string& text = "", const std::string& response = "") {
    std::thread([state, text, response]() {
        try {
            httplib::Client client("127.0.0.1", 8082);
            std::string cleaned_text = clean_for_json(text);
            std::string cleaned_response = clean_for_json(response);
            std::string body = "{\"state\": \"" + state + "\", \"text\": \"" + cleaned_text + "\", \"response\": \"" + cleaned_response + "\"}";
            client.Post("/api/voice_event", body, "application/json");
        } catch (...) {
            // Ignore connection errors if orchestrator is not running
        }
    }).detach();
}

// Helper to clean text for TTS (remove markdown and replace \n with pause)
std::string clean_for_tts(std::string input) {
    size_t pos = 0;
    while ((pos = input.find("\\n", pos)) != std::string::npos) {
        input.replace(pos, 2, ", ");
        pos += 2;
    }
    pos = 0;
    while ((pos = input.find("*", pos)) != std::string::npos) {
        input.replace(pos, 1, "");
    }
    pos = 0;
    while ((pos = input.find("#", pos)) != std::string::npos) {
        input.replace(pos, 1, "");
    }
    return input;
}

// Sherpa-onnx KWS
const SherpaOnnxKeywordSpotter* kws = nullptr;
const SherpaOnnxOnlineStream* kws_stream = nullptr;
const int SAMPLE_RATE = 16000;

void process_audio_pipeline(std::vector<float> wav_data) {
    status_message = "Processing STT...";
    current_state = STATE_PROCESSING_STT;
    report_voice_state("PROCESSING_STT");

    httplib::Client stt_client("127.0.0.1", 8080);
    // Build fake WAV header (44 bytes) just to satisfy our basic parser in STT service
    std::string wav_body(44, '\0');
    for (float f : wav_data) {
        int16_t sample = (int16_t)(f * 32767.0f);
        wav_body.append((char*)&sample, sizeof(int16_t));
    }

    auto stt_res = stt_client.Post("/transcribe", wav_body, "application/octet-stream");
    if (!stt_res || stt_res->status != 200) {
        status_message = "STT Failed.";
        current_state = STATE_IDLE;
        return;
    }

    // Extract basic JSON
    std::string json = stt_res->body;
    size_t pos = json.find("\"text\"");
    if (pos != std::string::npos) {
        pos = json.find("\"", pos + 6) + 1;
        size_t end = json.find("\"", pos);
        transcript_text = json.substr(pos, end - pos);
    }
    report_voice_state("PROCESSING_LLM", transcript_text);

    status_message = "Querying LLM...";
    current_state = STATE_PROCESSING_LLM;

    // ── Build LLM context prompt generically from telemetry config ─────────────
    std::vector<FieldDef>                      fields_snap;
    std::map<std::string, double>              vals_snap;
    std::map<std::string, std::vector<double>> arr_snap;
    {
        std::lock_guard<std::mutex> lock(telemetry_mutex);
        fields_snap = g_field_defs;
        vals_snap   = g_values;
        arr_snap    = g_array_values;
    }

    std::string system_prompt = "You are the EdgeDrive vehicle AI Voice Assistant connected to the CAN bus. Current telemetry: ";
    for (const auto& fd : fields_snap) {
        if (!fd.llm_context) continue;
        std::string val_str;
        if (fd.type == FieldType::GRID_4) {
            auto it = arr_snap.find(fd.key);
            if (it != arr_snap.end()) {
                val_str = "(";
                for (size_t i = 0; i < it->second.size() && i < 4; i++) {
                    if (i > 0) val_str += ", ";
                    std::string lbl = (i < fd.sublabels.size()) ? fd.sublabels[i] : std::to_string(i);
                    val_str += lbl + "=" + std::to_string((int)it->second[i]);
                }
                val_str += ")";
            }
        } else if (fd.type == FieldType::BOOLEAN) {
            auto it = vals_snap.find(fd.key);
            val_str = (it != vals_snap.end() && it->second > 0.5) ? fd.true_label : fd.false_label;
        } else {
            auto it = vals_snap.find(fd.key);
            val_str = (it != vals_snap.end()) ? std::to_string((int)it->second) : "0";
        }
        system_prompt += fd.label + ": " + val_str + " " + fd.unit + ", ";
    }
    system_prompt += ". Keep answers brief and natural.";

    // Send to Ollama /api/chat
    httplib::Client ollama_client("127.0.0.1", 11434);
    
    // Construct messages JSON
    std::string messages_json = "[";
    for (const auto& msg : chat_history) {
        messages_json += "{\"role\": \"" + msg.first + "\", \"content\": \"" + msg.second + "\"},";
    }
    
    // Inject system prompt directly into the final user query message content to ensure Gemma processes it
    std::string full_user_content = "[System Instruction: " + system_prompt + "] User query: " + transcript_text;
    messages_json += "{\"role\": \"user\", \"content\": \"" + clean_for_json(full_user_content) + "\"}]";
    
    std::string ollama_body = "{\"model\":\"gemma:2b\", \"messages\":" + messages_json + ", \"stream\":false}";
    
    auto ollama_res = ollama_client.Post("/api/chat", ollama_body, "application/json");
    if (!ollama_res || ollama_res->status != 200) {
        status_message = "LLM Failed.";
        current_state = STATE_IDLE;
        return;
    }

    json = ollama_res->body;
    pos = json.find("\"content\"");
    if (pos != std::string::npos) {
        pos = json.find("\"", pos + 9) + 1;
        size_t end = json.find("\"", pos);
        llm_response_text = json.substr(pos, end - pos);
        display_response_text = unescape_json_string(llm_response_text);
        
        // Add to history
        chat_history.push_back({"user", transcript_text});
        chat_history.push_back({"assistant", llm_response_text});
        
        // Keep history manageable (e.g. last 10 messages)
        if (chat_history.size() > 20) chat_history.erase(chat_history.begin(), chat_history.begin() + 2);
    }
    report_voice_state("SPEAKING", transcript_text, display_response_text);

    status_message = "Speaking...";
    current_state = STATE_SPEAKING;

    // Send to TTS
    httplib::Client tts_client("127.0.0.1", 8081);
    std::string cleaned_tts = clean_for_tts(llm_response_text);
    std::string tts_body = "{\"text\":\"" + cleaned_tts + "\"}";
    tts_client.Post("/speak", tts_body, "application/json");

    status_message = "Waiting for wake word...";
    current_state = STATE_IDLE;
    report_voice_state("IDLE");
}

// Audio capture callback
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    if (!pInput) return;
    const float* pcm = (const float*)pInput;

    if (current_state == STATE_IDLE || current_state == STATE_SPEAKING) {
        // Feed into KWS
        SherpaOnnxOnlineStreamAcceptWaveform(kws_stream, SAMPLE_RATE, pcm, frameCount);
        while (SherpaOnnxIsKeywordStreamReady(kws, kws_stream)) {
            SherpaOnnxDecodeKeywordStream(kws, kws_stream);
        }
        
        const SherpaOnnxKeywordResult* r = SherpaOnnxGetKeywordResult(kws, kws_stream);
        if (strlen(r->keyword) > 0) {
            std::string kw = r->keyword;
            std::cout << "Wake word detected: " << kw << std::endl;
            SherpaOnnxResetKeywordStream(kws, kws_stream);
            
            // Check if it is a stop command
            if (kw.find("stop") != std::string::npos) {
                // Send stop to TTS
                httplib::Client tts_client("127.0.0.1", 8081);
                tts_client.Post("/stop", "", "application/json");
                status_message = "Playback stopped.";
                current_state = STATE_IDLE;
                report_voice_state("IDLE");
            } else {
                // Regular wake word
                if (current_state == STATE_SPEAKING) {
                    httplib::Client tts_client("127.0.0.1", 8081);
                    tts_client.Post("/stop", "", "application/json");
                }
                current_state = STATE_LISTENING;
                report_voice_state("LISTENING");
                status_message = "Listening...";
                speech_duration = 0.0f;
                silence_duration = 0.0f;
                std::lock_guard<std::mutex> lock(audio_mutex);
                audio_buffer.clear();
            }
        }
        SherpaOnnxDestroyKeywordResult(r);
    } 
    else if (current_state == STATE_LISTENING) {
        std::lock_guard<std::mutex> lock(audio_mutex);
        audio_buffer.insert(audio_buffer.end(), pcm, pcm + frameCount);
        
        // Simple VAD based on energy
        float energy = 0.0f;
        for (ma_uint32 i = 0; i < frameCount; i++) {
            energy += std::abs(pcm[i]);
        }
        energy /= frameCount;
        
        if (energy < SILENCE_THRESHOLD) {
            silence_duration += (float)frameCount / SAMPLE_RATE;
        } else {
            silence_duration = 0.0f;
            speech_duration += (float)frameCount / SAMPLE_RATE;
        }
        
        // Auto-stop listening if spoken for a bit, followed by 1.5 seconds of silence
        if (speech_duration > 0.5f && silence_duration > 1.5f) {
            current_state = STATE_PROCESSING_STT;
            std::vector<float> audio_copy = audio_buffer;
            std::thread(process_audio_pipeline, audio_copy).detach();
        }
    }
}

void InitializeKWS() {
    SherpaOnnxKeywordSpotterConfig config;
    memset(&config, 0, sizeof(config));
    
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    
    // Paths to models (assuming extracted to external/sherpa-onnx-kws-model)
    config.model_config.transducer.encoder = "external/sherpa-onnx-kws-model/encoder-epoch-12-avg-2-chunk-16-left-64.onnx";
    config.model_config.transducer.decoder = "external/sherpa-onnx-kws-model/decoder-epoch-12-avg-2-chunk-16-left-64.onnx";
    config.model_config.transducer.joiner = "external/sherpa-onnx-kws-model/joiner-epoch-12-avg-2-chunk-16-left-64.onnx";
    config.model_config.tokens = "external/sherpa-onnx-kws-model/tokens.txt";
    config.model_config.num_threads = 1;
    config.model_config.debug = 0;
    
    config.keywords_file = "external/sherpa-onnx-kws-model/keywords.txt";
    config.keywords_score = 1.0f;
    config.keywords_threshold = 0.25f;
    
    kws = SherpaOnnxCreateKeywordSpotter(&config);
    kws_stream = SherpaOnnxCreateKeywordStream(kws);
}

// ─── Generic Dashboard Render Helpers ──────────────────────────────────
static void render_field_progress_bar(const FieldDef& fd, double value) {
    float fv = (float)value;
    bool warn = (fd.warn_above < FLT_MAX && fv >= fd.warn_above) ||
                (fd.warn_below > -FLT_MAX && fv <= fd.warn_below);
    if (warn && !fd.warn_label.empty())
        ImGui::TextColored(ImVec4(1.0f,0.2f,0.2f,1.0f), "%s: %.1f %s  [%s]", fd.label.c_str(), fv, fd.unit.c_str(), fd.warn_label.c_str());
    else
        ImGui::Text("%s: %.1f %s", fd.label.c_str(), fv, fd.unit.c_str());
    float ratio = (fd.max_val > 0.0f) ? (fv / fd.max_val) : 0.0f;
    ImGui::ProgressBar(ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio), ImVec2(-FLT_MIN, 20.0f));
    ImGui::Spacing();
}

static void render_field_text(const FieldDef& fd, double value) {
    float fv = (float)value;
    bool warn = (fd.warn_above < FLT_MAX && fv >= fd.warn_above) ||
                (fd.warn_below > -FLT_MAX && fv <= fd.warn_below);
    if (warn && !fd.warn_label.empty())
        ImGui::TextColored(ImVec4(1.0f,0.3f,0.3f,1.0f), "%s: %.1f %s  [%s]", fd.label.c_str(), fv, fd.unit.c_str(), fd.warn_label.c_str());
    else
        ImGui::Text("%s: %.1f %s", fd.label.c_str(), fv, fd.unit.c_str());
    ImGui::Spacing();
}

static void render_field_boolean(const FieldDef& fd, double value) {
    bool bv = value > 0.5;
    ImGui::Text("%s: ", fd.label.c_str()); ImGui::SameLine();
    ImGui::TextColored(bv ? ImVec4(0.2f,0.8f,0.2f,1.0f) : ImVec4(0.5f,0.5f,0.5f,1.0f),
                       "%s", bv ? fd.true_label.c_str() : fd.false_label.c_str());
    ImGui::Spacing();
}

static void render_field_grid4(const FieldDef& fd, const std::vector<double>& values) {
    ImGui::Text("%s (%s):", fd.label.c_str(), fd.unit.c_str());
    ImGui::Columns(2, ("grid_" + fd.key).c_str(), false);
    for (size_t i = 0; i < values.size() && i < 4; i++) {
        std::string lbl = (i < fd.sublabels.size()) ? fd.sublabels[i] : std::to_string(i);
        bool warn = (fd.warn_below > -FLT_MAX && values[i] <= fd.warn_below);
        ImGui::Text("%s:", lbl.c_str()); ImGui::SameLine();
        ImGui::TextColored(warn ? ImVec4(1.0f,0.2f,0.2f,1.0f) : ImVec4(0.2f,0.8f,0.2f,1.0f),
                           "%.1f", (float)values[i]);
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
    ImGui::Spacing();
}

int main() {
    // Setup KWS
    InitializeKWS();

    // Fetch field definitions from orchestrator config endpoint (one-time at startup)
    fetch_telemetry_config();

    // Start background telemetry worker thread (10Hz)
    std::thread telemetry_thread(telemetry_fetch_worker);

    // Setup Miniaudio
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = 1;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;

    ma_device device;
    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        std::cerr << "Failed to initialize capture device." << std::endl;
        return -1;
    }
    ma_device_start(&device);

    // Setup ImGui + DX11 window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"Assistant GUI", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Voice Assistant Simulator", WS_OVERLAPPEDWINDOW, 100, 100, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Main loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 1. Assistant UI Window (Left Half)
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(450.0f, io.DisplaySize.y));
        ImGui::Begin("Assistant Panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Voice Assistant - Microservices Dashboard");
        if (!display_response_text.empty()) {
            ImGui::TextWrapped("Assistant: %s", display_response_text.c_str());
        }
        ImGui::Separator();
        
        ImGui::Text("Status: %s", status_message.c_str());
        
        if (current_state == STATE_LISTENING) {
            if (ImGui::Button("Stop Listening & Process", ImVec2(200, 50))) {
                current_state = STATE_PROCESSING_STT;
                std::vector<float> audio_copy;
                {
                    std::lock_guard<std::mutex> lock(audio_mutex);
                    audio_copy = audio_buffer;
                }
                std::thread(process_audio_pipeline, audio_copy).detach();
            }
        }
        
        ImGui::Spacing();
        ImGui::Text("User Transcript:");
        ImGui::InputTextMultiline("##transcript", (char*)transcript_text.c_str(), transcript_text.capacity(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4), ImGuiInputTextFlags_ReadOnly);

        ImGui::Spacing();
        ImGui::Text("Assistant Response:");
        ImGui::InputTextMultiline("##response", (char*)llm_response_text.c_str(), llm_response_text.capacity(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8), ImGuiInputTextFlags_ReadOnly);

        ImGui::End();

        // 2. Vehicle Telemetry Dashboard Window (Right Half) — driven by /api/telemetry_config
        ImGui::SetNextWindowPos(ImVec2(450.0f, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 450.0f, io.DisplaySize.y));
        ImGui::Begin("Vehicle Telemetry Dashboard", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::TextColored(ImVec4(0.2f, 0.6f, 0.9f, 1.0f), "Vehicle Telemetry Dashboard");
        ImGui::Separator();
        ImGui::Spacing();

        // Snapshot all state under lock
        std::vector<FieldDef>                      render_fields;
        std::map<std::string, double>              render_vals;
        std::map<std::string, std::vector<double>> render_arr_vals;
        {
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            render_fields   = g_field_defs;
            render_vals     = g_values;
            render_arr_vals = g_array_values;
        }

        if (render_fields.empty()) {
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.2f,1.0f),
                "Waiting for telemetry config from orchestrator...");
        }

        // Generic render loop — no hardcoded field names
        for (const auto& fd : render_fields) {
            static const std::vector<double> empty_vec;
            switch (fd.type) {
                case FieldType::PROGRESS_BAR: {
                    auto it = render_vals.find(fd.key);
                    render_field_progress_bar(fd, it != render_vals.end() ? it->second : 0.0);
                    break;
                }
                case FieldType::TEXT: {
                    auto it = render_vals.find(fd.key);
                    render_field_text(fd, it != render_vals.end() ? it->second : 0.0);
                    break;
                }
                case FieldType::BOOLEAN: {
                    auto it = render_vals.find(fd.key);
                    render_field_boolean(fd, it != render_vals.end() ? it->second : 0.0);
                    break;
                }
                case FieldType::GRID_4: {
                    auto it = render_arr_vals.find(fd.key);
                    render_field_grid4(fd, it != render_arr_vals.end() ? it->second : empty_vec);
                    break;
                }
            }
        }

        ImGui::End();

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    telemetry_thread_running = false;
    if (telemetry_thread.joinable()) {
        telemetry_thread.join();
    }

    ma_device_uninit(&device);
    SherpaOnnxDestroyOnlineStream(kws_stream);
    SherpaOnnxDestroyKeywordSpotter(kws);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// DX11 Boilerplate
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
