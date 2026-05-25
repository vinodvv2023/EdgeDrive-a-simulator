#include "httplib.h"
#include <sapi.h>
#include <iostream>
#include <string>
#include <windows.h>
#include <mutex>

std::string extract_json_text(const std::string& json) {
    size_t pos = json.find("\"text\"");
    if (pos == std::string::npos) return "";
    pos = json.find("\"", pos + 6);
    if (pos == std::string::npos) return "";
    pos++;
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

ISpVoice* g_pVoice = nullptr;
std::mutex g_voiceMutex;

int main() {
    // Initialize COM in Multithreaded Apartment (MTA) so worker threads can access g_pVoice
    if (FAILED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        std::cerr << "Failed to initialize COM" << std::endl;
        return 1;
    }

    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&g_pVoice))) {
        std::cerr << "Failed to create ISpVoice instance" << std::endl;
        ::CoUninitialize();
        return 1;
    }
    std::cout << "Windows SAPI initialized successfully." << std::endl;

    httplib::Server svr;
    
    svr.Post("/speak", [](const httplib::Request& req, httplib::Response& res) {
        std::string text = extract_json_text(req.body);
        if (text.empty()) {
            res.set_content("{\"status\": \"error\"}", "application/json");
            return;
        }
        
        std::cout << "Speaking: " << text << std::endl;
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring wtext(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], wlen);

        if (SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            std::lock_guard<std::mutex> lock(g_voiceMutex);
            g_pVoice->Speak(wtext.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
            ::CoUninitialize();
        }

        res.set_content("{\"status\": \"ok\"}", "application/json");
    });

    svr.Post("/stop", [](const httplib::Request& req, httplib::Response& res) {
        std::cout << "Stopping TTS playback..." << std::endl;
        if (SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            std::lock_guard<std::mutex> lock(g_voiceMutex);
            g_pVoice->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
            ::CoUninitialize();
        }
        res.set_content("{\"status\": \"ok\"}", "application/json");
    });

    std::cout << "TTS Service listening on http://127.0.0.1:8081..." << std::endl;
    svr.listen("127.0.0.1", 8081);

    g_pVoice->Release();
    ::CoUninitialize();
    return 0;
}
