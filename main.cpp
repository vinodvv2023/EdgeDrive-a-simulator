#include "httplib.h"

int main() {
    httplib::Server svr;

    svr.Post("/transcribe", [](const httplib::Request& req, httplib::Response& res) {
        // req.body = audio bytes; call Whisper.cpp here
        std::string text = "..."; // result of transcription
        res.set_content("{\"text\": \"" + text + "\"}", "application/json");
    });

    svr.listen("127.0.0.1", 8080);
}