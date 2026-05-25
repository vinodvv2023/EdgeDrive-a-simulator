# Walkthrough - Query Vehicle Status & Telemetry Dashboard Window

We have successfully implemented live vehicle status queries and a real-time side-by-side Telemetry Dashboard in the C++ Voice Assistant.

---

## 1. Accomplishments

### Live Telemetry Cache & API Endpoint
* Modified [server.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/orchestrator/server.py) to declare a global `vehicle_state` dictionary cache.
* Updated `vehicle_state` in real-time inside the WebSocket simulator connection handler whenever telemetry packets arrive.
* Exposed a FastAPI HTTP GET endpoint `/api/vehicle_status` returning this cache as JSON.

### C++ Telemetry Dashboard & Worker Thread
* Added a thread-safe `VehicleTelemetry` struct inside [assistant_gui.cpp](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/assistant_gui.cpp).
* Implemented a background thread `telemetry_fetch_worker()` that fetches data from `/api/vehicle_status` at 10Hz (every 100ms) using a non-blocking `httplib` request and updates the local telemetry cache safely.
* Modified the ImGui render loop in [assistant_gui.cpp](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/assistant_gui.cpp) to partition the UI into a dual-pane layout:
  * **Left Side (Voice Assistant):** Handles wake-word alerts, transcription, and LLM chat responses.
  * **Right Side (Vehicle Telemetry Dashboard):** Displays engine running/stopped status, current gear, speed (with custom red alerting on speed alarm), RPM, and visual progress bar meters for both Speed and RPM.

### Telemetry Context Injection into LLM
* In the C++ voice pipeline, we retrieve the thread-safe telemetry snapshot and construct a detailed system instruction prefix:
  `"Current vehicle telemetry state: Speed: <speed> KMPH, RPM: <rpm>, Gear: <gear>, Engine Status: <started>, Speed Alarm: <speed_alarm>."`
* Prepended this `system` prompt block as the first message in the `/api/chat` request sent to Ollama (`gemma:2b`), enabling the offline model to correctly answer driver questions about vehicle status.

---

## 2. Compilation Instructions

To compile the updated GUI application, run the following command in your terminal from the project root:

```powershell
cmake --build build --config Release --target assistant_gui
```

---

## 3. Verification Steps

1. **Launch Services:** Boot the microservices by running `.\run_assistant.bat`.
2. **Telemetry Dashboard Verification:**
   * Verify that the GUI split-pane shows the new `"Vehicle Telemetry Dashboard"` panel on the right.
   * Drive the vehicle (press keys in the simulator command line/dashboard) and verify that the Speed and RPM progress bars update instantly at 10Hz.
   * Accelerate past 120 km/h and verify the speed indicator turns red showing `(ALARM ACTIVE!)`.
3. **Voice Queries Verification:**
   * Speak the wake word (*"Covesa"*) or press the trigger button.
   * Ask: *"What is my current speed?"* or *"What gear am I in?"*
   * Verify that the voice assistant correctly retrieves the values and speaks/displays the exact state (e.g. *"You are currently driving at 65 KMPH in gear 3"*).
