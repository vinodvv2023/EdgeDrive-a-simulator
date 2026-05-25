# Implementation Plan - Advanced Vehicle Data Simulation & TTS Warnings

This plan outlines how to simulate new vehicle sensors (tyre pressures, radiator coolant temperature, cooling fan status, fuel level depletion) inside the CAN simulator, trigger automated spoken warnings (TTS) in the orchestrator, and display them visually inside the C++ GUI.

---

## Proposed Architecture

```mermaid
graph TD
    subgraph CAN Simulator [simulator/main.py]
        Physics[Engine started loop]
        Physics -->|1-min timer| LowFuel[Deplete fuel to 15%]
        Physics -->|Slow leak| DeflateTyre[Leak Front-Left Tyre pressure]
        Physics -->|Temp rise| HeatCoolant[Raise Coolant Temp to 95C]
        HeatCoolant -->|Activate Fan| CoolingFan[Turn Fan On]
    end

    CAN Simulator -->|Websocket /ws/dashboard| Orch[Orchestrator: server.py]
    
    subgraph Orchestrator [orchestrator/server.py]
        Monitor[Monitor telemetry stream]
        Monitor -->|Fuel drops below 15%| WarnFuel[TTS Warn: 'Fuel level is low']
        Monitor -->|Tyre drops below 24 PSI| WarnTyre[TTS Warn: 'Tyre pressure critically low']
        WarnFuel & WarnTyre -->|HTTP POST /speak| TTS[TTS Service: Port 8081]
    end

    Orch -->|HTTP GET /api/vehicle_status| GUI[C++ Voice Assistant: assistant_gui.cpp]
    GUI -->|Render Grid & Progress Bars| VisualDashboard[ImGui Side Panel Dashboard]
    GUI -->|Inject Prompt Context| LLM[Ollama: gemma:2b]
```

1. **CAN Simulator ([main.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/simulator/main.py)):**
   * **Fuel Level:** Start at 100.0%. When engine is running, deplete fuel by `0.1417%` per step (`~0.85%` per second) so it drops to exactly 15.0% after 1 minute of run time.
   * **Tyre Pressures:** Declare a 4-float array `[FL, FR, RL, RR]` initialized to `32.0` PSI. Slowly deflate the Front-Left tyre by `0.05` PSI per step (`0.5` PSI per second) when the engine is running to simulate a leak.
   * **Coolant Temp & Fan:** Start coolant temperature at 40°C. When running, heat it by `0.05°C` per step. Once it hits 95°C, toggle `cooling_fan = True` and slow/stabilize the heat increase.

2. **Orchestrator Alerts ([server.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/orchestrator/server.py)):**
   * Monitor the incoming JSON stream.
   * Trigger a single welcome warning speech via the TTS server `/speak` endpoint when:
     * `fuel_level` drops below 15.0% (e.g. *"Warning: Fuel level is low. Please refuel."*).
     * Any tyre pressure in `tyre_pressures` falls below 24.0 PSI (e.g. *"Warning: Front Left tyre pressure is critically low at 23 PSI. Please check your tyres."*).
   * Guard these TTS alerts with state tracking flags to prevent duplicate/spam warnings.

3. **C++ GUI Client ([assistant_gui.cpp](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/assistant_gui.cpp)):**
   * Expand the local thread-safe `VehicleTelemetry` structure to hold the new fields.
   * Update [parse_json_float()](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/assistant_gui.cpp#L66) and [parse_json_int()](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/assistant_gui.cpp#L76) to delimit parsing by `",}]"` to handle parsing values within arrays.
   * Update the background thread `telemetry_fetch_worker()` to extract these new parameters.
   * Update the side-panel `"Vehicle Telemetry Dashboard"` layout to render:
     * **Fuel Status:** Displays level percentage and a horizontal progress bar (turning red when fuel is low).
     * **Coolant Status:** Displays coolant temperature and fan state (active/off).
     * **Tyre Pressures:** Displays a 2x2 grid matching the car layout (FL, FR, RL, RR) with values turning red if pressure falls below critical limits.
   * Include the new status indicators inside the context prompt prepended to user voice requests so the LLM is fully aware of them.

---

## Proposed Changes

### 1. CAN Simulator Update
#### [MODIFY] [main.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/simulator/main.py)
* Declare variables `fuel_level`, `coolant_temp`, `cooling_fan`, and `tyre_pressures`.
* Update these states dynamically in `simulate()` whenever `is_started` is True.
* Include these new fields in the JSON telemetry packet sent over WebSocket and MQTT.

### 2. Orchestrator Update
#### [MODIFY] [server.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/orchestrator/server.py)
* Add the new metrics to the global `vehicle_state` cache.
* Track warning flag states: `has_warned_fuel = False` and `has_warned_tyre = False`.
* In `connect_to_simulator()`:
  * Extract the new states.
  * Check thresholds and trigger a background thread to speak a TTS message if a warning condition is met for the first time.
  * Reset flags if the engine is stopped/restarted.

### 3. C++ GUI Update
#### [MODIFY] [assistant_gui.cpp](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/assistant_gui.cpp)
* Expand the `VehicleTelemetry` struct definition and the parsing logic inside `telemetry_fetch_worker()`.
* Add ImGui drawing calls inside the `Vehicle Telemetry Dashboard` rendering block for Fuel progress bar, Coolant info, and Tyre grid.
* Update `system_prompt` construction inside `process_audio_pipeline()` to describe the tyre pressures, fuel level, coolant temperature, and fan status.

---

## Verification Plan

### Automated / Manual Verification
1. **Compilation (To be run by user):**
   ```powershell
   cmake --build build --config Release --target assistant_gui
   ```
2. **Start Services:** Start all services via `run_assistant.bat`.
3. **Dashboard Window Verification:**
   * Observe that a new window titled `"Vehicle Telemetry Dashboard"` appears in the GUI.
   * Drive the simulator and verify the speed/RPM progress bars and alarms react instantly at 10Hz.
4. **Manual Query Testing:**
   * Accelerate the vehicle to 60 km/h.
   * Ask the voice assistant: *"How fast am I driving?"*
   * Verify that the spoken response matches the current speed on the dashboard.
