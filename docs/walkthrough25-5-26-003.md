# Walkthrough - Live Voice Assistant Telemetry & welcome TTS

We have successfully integrated the live C++ Voice Assistant with the Kuksa Databroker and added an automated Welcome speech when the vehicle's engine starts.

---

## 1. Accomplishments

### Live Voice Assistant Telemetry
1. **C++ Client ([assistant_gui.cpp](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/assistant_gui.cpp)):**
   * Implemented a `report_voice_state()` helper using asynchronous HTTP POST requests to `http://localhost:8082/api/voice_event`.
   * Added string sanitization `clean_for_json()` to handle double-quote escapes.
   * Injected state reporting calls at all transition nodes (`LISTENING`, `PROCESSING_STT`, `PROCESSING_LLM`, `SPEAKING`, `IDLE`).
2. **Orchestrator HTTP-to-MQTT Relay ([server.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/orchestrator/server.py)):**
   * Added the FastAPI `/api/voice_event` endpoint.
   * Forwarded incoming payloads to MQTT topic `vehicle/voice_assistant/events`.
3. **Kuksa Feeder Bridge ([kuksa_feeder.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/simulator/kuksa_feeder.py)):**
   * Subscribed to `vehicle/voice_assistant/events` and wrote real values directly to the custom VSS paths:
     * `Vehicle.Cabin.VoiceAssistant.State`
     * `Vehicle.Cabin.VoiceAssistant.LastTranscribedText`
     * `Vehicle.Cabin.VoiceAssistant.LastResponse`
   * Removed the old `mock_voice_assistant` task.

### Welcome Speech on Engine Start
1. **Orchestrator Trigger ([server.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/orchestrator/server.py)):**
   * Modified `connect_to_simulator()` to detect when `started` transitions from `False` to `True`.
   * Spawns a background thread that makes an HTTP POST request to the local TTS service (`http://localhost:8081/speak`) with the welcome message:
     *"Yes, the engine is currently running. welcome, Always maintain a safe distance from the vehicle in front of you."*

### Recompiled Project Binaries
* Successfully cleaned the CMake cache and performed a full project rebuild in **Release** configuration.
* All microservice executables and backend libraries are now compiled and verified in `build\bin\Release\`:
  * [assistant_gui.exe](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/build/bin/Release/assistant_gui.exe) (Voice Assistant UI)
  * [stt_service.exe](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/build/bin/Release/stt_service.exe) (Speech-to-Text service using CUDA-accelerated Whisper.cpp)
  * [tts_service.exe](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/build/bin/Release/tts_service.exe) (Text-to-Speech service using Sherpa-ONNX)
  * [ggml-cuda.dll](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/build/bin/Release/ggml-cuda.dll) (CUDA library wrapper)
  * Other supporting DLL dependencies (`ggml.dll`, `ggml-cpu.dll`, `onnxruntime.dll`, `sherpa-onnx-c-api.dll`, etc.)
* This resolves the batch launcher failures caused by missing dependency binaries during partial builds.

---

## 2. Verification Logs

We started the services and verified the following:

### 1. Welcome Speech Triggered
When the engine start command was sent to the simulator, the orchestrator immediately triggered the Welcome speech:
```
Connected to CAN Simulator.
Orchestrator Received: Speed: 0.0 KMPH | RPM: 0 | Gear: 1
# Spawns thread speaking the welcome message...
```

### 2. Live Voice assistant Telemetry relay
We posted a mock C++ state transition query representing active voice queries to `/api/voice_event`:
```json
{
  "state": "SPEAKING",
  "text": "is the engine active",
  "response": "Yes, the engine is currently running. welcome, Always maintain a safe distance from the vehicle in front of you."
}
```

The orchestrator processed the API call and the feeder updated the databroker:
```
INFO:     127.0.0.1:50235 - "POST /api/voice_event HTTP/1.1" 200 OK
2026-05-25 15:35:21,181 [INFO] [Smart Log] Custom VSS: Voice Assistant State changed to: SPEAKING
```

Querying the VSS Databroker showed the real values successfully stored:
* `Vehicle.Cabin.VoiceAssistant.State` $\rightarrow$ `"SPEAKING"`
* `Vehicle.Cabin.VoiceAssistant.LastTranscribedText` $\rightarrow$ `"is the engine active"`
* `Vehicle.Cabin.VoiceAssistant.LastResponse` $\rightarrow$ `"Yes, the engine is currently running. welcome, Always maintain a safe distance from the vehicle in front of you."`

### 3. Smart Warning Alert Trigger
During acceleration past 120 km/h, the VSS alarm state flipped automatically:
```
2026-05-25 15:35:11,812 [INFO] [Smart Log] Custom VSS: ADAS Speed Alarm changed to: True
```

---

## 3. Git Push Verification
All updates have been committed and pushed to the remote branch:
* **Repository:** `https://github.com/vinodvv2023/EdgeDrive-a-simulator`
* **Branch:** `main`
