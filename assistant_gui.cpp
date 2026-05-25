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

// Telemetry structure & worker globals
struct VehicleTelemetry {
    float speed = 0.0f;
    int rpm = 0;
    int gear = 1;
    bool started = false;
    bool speed_alarm = false;
};

VehicleTelemetry g_telemetry;
std::mutex telemetry_mutex;
bool telemetry_thread_running = true;

// Helper to parse JSON values from orchestrator status response
bool parse_json_bool(const std::string& str, size_t pos) {
    std::string sub = str.substr(pos, 15);
    return sub.find("true") != std::string::npos;
}

float parse_json_float(const std::string& str, size_t pos) {
    size_t i = pos + 1;
    while (i < str.size() && (str[i] == ' ' || str[i] == ':')) i++;
    try {
        return std::stof(str.substr(i));
    } catch (...) {
        return 0.0f;
    }
}

int parse_json_int(const std::string& str, size_t pos) {
    size_t i = pos + 1;
    while (i < str.size() && (str[i] == ' ' || str[i] == ':')) i++;
    try {
        return std::stoi(str.substr(i));
    } catch (...) {
        return 0;
    }
}

void telemetry_fetch_worker() {
    while (telemetry_thread_running) {
        try {
            httplib::Client client("localhost", 8082);
            client.set_connection_timeout(0, 500000); // 500ms
            client.set_read_timeout(0, 500000); // 500ms
            
            auto res = client.Get("/api/vehicle_status");
            if (res && res->status == 200) {
                std::string body = res->body;
                float speed = 0.0f;
                int rpm = 0;
                int gear = 1;
                bool started = false;
                bool speed_alarm = false;
                
                size_t pos;
                if ((pos = body.find("\"speed\"")) != std::string::npos) {
                    pos = body.find(":", pos);
                    if (pos != std::string::npos) speed = parse_json_float(body, pos);
                }
                if ((pos = body.find("\"rpm\"")) != std::string::npos) {
                    pos = body.find(":", pos);
                    if (pos != std::string::npos) rpm = parse_json_int(body, pos);
                }
                if ((pos = body.find("\"gear\"")) != std::string::npos) {
                    pos = body.find(":", pos);
                    if (pos != std::string::npos) gear = parse_json_int(body, pos);
                }
                if ((pos = body.find("\"started\"")) != std::string::npos) {
                    pos = body.find(":", pos);
                    if (pos != std::string::npos) started = parse_json_bool(body, pos);
                }
                if ((pos = body.find("\"speed_alarm\"")) != std::string::npos) {
                    pos = body.find(":", pos);
                    if (pos != std::string::npos) speed_alarm = parse_json_bool(body, pos);
                }
                
                {
                    std::lock_guard<std::mutex> lock(telemetry_mutex);
                    g_telemetry.speed = speed;
                    g_telemetry.rpm = rpm;
                    g_telemetry.gear = gear;
                    g_telemetry.started = started;
                    g_telemetry.speed_alarm = speed_alarm;
                }
            }
        } catch (...) {
            // Ignore connection errors if server is starting/stopping
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
            httplib::Client client("localhost", 8082);
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

    httplib::Client stt_client("localhost", 8080);
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

    // Retrieve latest telemetry safely
    VehicleTelemetry tel;
    {
        std::lock_guard<std::mutex> lock(telemetry_mutex);
        tel = g_telemetry;
    }

    std::string system_prompt = "You are the EdgeDrive vehicle's AI Voice Assistant. You are connected to the vehicle's CAN bus and telemetry systems. "
                               "Current vehicle telemetry state: "
                               "Speed: " + std::to_string((int)tel.speed) + " KMPH, "
                               "RPM: " + std::to_string(tel.rpm) + ", "
                               "Gear: " + std::to_string(tel.gear) + ", "
                               "Engine Status: " + (tel.started ? "Running" : "Stopped") + ", "
                               "Speed Alarm: " + (tel.speed_alarm ? "Active" : "Inactive") + ". "
                               "Use this telemetry state to answer any questions the driver asks about the vehicle's speed, engine, gear, rpm, or alarms. Keep your answers brief and natural.";

    // Send to Ollama /api/chat
    httplib::Client ollama_client("localhost", 11434);
    
    // Construct messages JSON
    std::string messages_json = "[";
    messages_json += "{\"role\": \"system\", \"content\": \"" + clean_for_json(system_prompt) + "\"},";
    for (const auto& msg : chat_history) {
        messages_json += "{\"role\": \"" + msg.first + "\", \"content\": \"" + msg.second + "\"},";
    }
    messages_json += "{\"role\": \"user\", \"content\": \"" + transcript_text + "\"}]";
    
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
    httplib::Client tts_client("localhost", 8081);
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
                httplib::Client tts_client("localhost", 8081);
                tts_client.Post("/stop", "", "application/json");
                status_message = "Playback stopped.";
                current_state = STATE_IDLE;
                report_voice_state("IDLE");
            } else {
                // Regular wake word
                if (current_state == STATE_SPEAKING) {
                    httplib::Client tts_client("localhost", 8081);
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

int main() {
    // Setup KWS
    InitializeKWS();

    // Start background telemetry worker thread
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

        // 2. Vehicle Telemetry Dashboard Window (Right Half)
        ImGui::SetNextWindowPos(ImVec2(450.0f, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 450.0f, io.DisplaySize.y));
        ImGui::Begin("Vehicle Telemetry Dashboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        
        ImGui::TextColored(ImVec4(0.2f, 0.6f, 0.9f, 1.0f), "Vehicle Telemetry Dashboard");
        ImGui::Separator();
        ImGui::Spacing();

        VehicleTelemetry tel;
        {
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            tel = g_telemetry;
        }

        ImGui::Text("Engine Status: ");
        ImGui::SameLine();
        if (tel.started) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "RUNNING");
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "STOPPED");
        }

        ImGui::Text("Transmission Gear: ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "Gear %d", tel.gear);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Speed Meter
        if (tel.speed_alarm) {
            ImGui::TextColored(ImVec4(1.0f, 0.1f, 0.1f, 1.0f), "Speed: %.1f KMPH (ALARM ACTIVE!)", tel.speed);
        } else {
            ImGui::Text("Speed: %.1f KMPH", tel.speed);
        }
        ImGui::ProgressBar(tel.speed / 200.0f, ImVec2(-FLT_MIN, 25.0f), "Speed");

        ImGui::Spacing();

        // RPM Meter
        ImGui::Text("Engine Speed: %d RPM", tel.rpm);
        ImGui::ProgressBar((float)tel.rpm / 8000.0f, ImVec2(-FLT_MIN, 25.0f), "RPM");

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
