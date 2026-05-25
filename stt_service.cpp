#include "httplib.h"
#include "whisper.h"
#include <iostream>
#include <vector>
#include <string>

// Simple WAV parser: Skips 44 byte header and converts 16-bit PCM to float
std::vector<float> parse_wav_to_floats(const std::string& body) {
    std::vector<float> pcmf32;
    if (body.size() <= 44) return pcmf32;
    const int16_t* pcm16 = reinterpret_cast<const int16_t*>(body.data() + 44);
    size_t samples = (body.size() - 44) / sizeof(int16_t);
    pcmf32.resize(samples);
    for (size_t i = 0; i < samples; i++) {
        pcmf32[i] = (float)pcm16[i] / 32768.0f;
    }
    return pcmf32;
}

int main() {
    whisper_context_params cparams = whisper_context_default_params();
    // Assuming ggml-base.bin is located in the working directory
    whisper_context* ctx = whisper_init_from_file_with_params("ggml-base.bin", cparams);
    if (!ctx) {
        std::cerr << "Failed to initialize Whisper context. Ensure ggml-base.bin exists!" << std::endl;
        return 1;
    }
    std::cout << "Whisper context initialized successfully." << std::endl;

    httplib::Server svr;
    svr.Post("/transcribe", [ctx](const httplib::Request& req, httplib::Response& res) {
        std::vector<float> pcmf32 = parse_wav_to_floats(req.body);
        if (pcmf32.empty()) {
            res.set_content("{\"text\": \"\"}", "application/json");
            return;
        }

        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress = false;
        wparams.print_realtime = false;
        wparams.print_timestamps = false;
        wparams.no_context = true;
        wparams.language = "en";
        
        if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
            std::cerr << "Failed to process audio" << std::endl;
            res.set_content("{\"text\": \"\"}", "application/json");
            return;
        }

        std::string result = "";
        const int n_segments = whisper_full_n_segments(ctx);
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            result += text;
        }
        
        // Trim leading space and escape quotes
        if (!result.empty() && result[0] == ' ') result.erase(0, 1);
        std::string escaped;
        for (char c : result) {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }

        std::cout << "Transcribed: " << escaped << std::endl;
        res.set_content("{\"text\": \"" + escaped + "\"}", "application/json");
    });

    std::cout << "STT Service listening on http://127.0.0.1:8080..." << std::endl;
    svr.listen("127.0.0.1", 8080);

    whisper_free(ctx);
    return 0;
}
