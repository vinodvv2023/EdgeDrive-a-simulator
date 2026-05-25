# Walkthrough - MQTT Integration, Custom VSS Telemetry, and Smart Event Logging

We have successfully integrated an MQTT messaging pipeline, injected custom VSS paths into the Kuksa Databroker, and implemented smart event-driven logging to replicate a production-grade telematics architecture.

---

## 1. Summary of Changes

### VSS Injection Script
* Created [inject_custom_vss.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/Kuksa-vss-data/inject_custom_vss.py), which programmatically loads the base VSS tree, registers our new custom nodes under `Vehicle.Cabin` and `Vehicle.ADAS`, and outputs [custom_vss.json](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/Kuksa-vss-data/custom_vss.json).
* Custom paths registered:
  * `Vehicle.Cabin.VoiceAssistant.State` (string)
  * `Vehicle.Cabin.VoiceAssistant.LastTranscribedText` (string)
  * `Vehicle.Cabin.VoiceAssistant.LastResponse` (string)
  * `Vehicle.ADAS.Cabin.IsSpeedAlarmActive` (boolean)

### MQTT Pub/Sub Pipeline
* **MQTT Broker:** Integrated Eclipse Mosquitto docker image running on port `1883`.
* **CAN Simulator ([main.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/simulator/main.py)):** Modified to publish raw 10Hz simulator state updates to MQTT topic `vehicle/simulator/telemetry`.
* **CAN Orchestrator ([server.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/orchestrator/server.py)):** Modified to publish safety alarm triggers (`is_over_120`) to MQTT topic `vehicle/orchestrator/alarms`.

### Smart Event-Driven Feeder Bridge ([kuksa_feeder.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/simulator/kuksa_feeder.py))
* Overwrote the feeder to subscribe to the MQTT broker topics instead of connecting via raw WebSockets.
* **Smart Logging Filters:**
  * **Speed:** Only publishes to Kuksa if speed changes by $\ge 3.0$ km/h, OR if 15 seconds elapse without an update (heartbeat).
  * **RPM:** Only publishes to Kuksa if engine speed changes by $\ge 200$ rpm.
  * **State Changes:** Updates instantly if `started`, `gear`, or `IsSpeedAlarmActive` change.
* **Voice Assistant Mock Scenario:** Periodically simulates assistant queries (e.g. state transitions: `LISTENING` $\rightarrow$ `PROCESSING` $\rightarrow$ `SPEAKING` $\rightarrow$ `IDLE`) and feeds transcription and LLM response text into the custom VSS paths.

---

## 2. Verification Results

We started all services (Mosquitto, Kuksa Databroker, Simulator, Orchestrator, and Feeder) and verified:

### 1. Custom VSS Schema Support
Reading from the Databroker successfully returned the custom Voice Assistant nodes and initial alarm status:
```
Vehicle.ADAS.Cabin.IsSpeedAlarmActive -> False
Vehicle.Cabin.VoiceAssistant.State -> IDLE
```

### 2. Smart Logging (Reduced Data Traffic)
Instead of streaming 10 times a second to Kuksa, the feeder only sent logs when speed changes were significant or state changed:
```
2026-05-25 13:06:13,919 [INFO] [Smart Log] Speed update triggered: 0.0 km/h (Delta or Heartbeat)
2026-05-25 13:06:13,919 [INFO] [Smart Log] Engine state changed to: False
2026-05-25 13:06:13,919 [INFO] [Smart Log] Transmission Gear changed to: 1
```

### 3. Voice Assistant State Simulation
```
2026-05-25 13:06:23,884 [INFO] [Voice Assistant Mock] Detected wake word 'Covesa'...
2026-05-25 13:06:25,909 [INFO] [Voice Assistant Mock] Transcribed STT: 'is the engine active'
2026-05-25 13:06:29,941 [INFO] [Voice Assistant Mock] Assistant replied: 'Yes, the engine is currently running.'
2026-05-25 13:06:33,958 [INFO] [Voice Assistant Mock] State reset to IDLE
```

### 4. Custom Alarm Verification
Publishing a mock speed warning alert successfully updated the custom path:
```
2026-05-25 13:06:43,779 [INFO] [Smart Log] Custom VSS: ADAS Speed Alarm changed to: True
```

---

## 3. How to Run

Simply launch the updated launcher batch file:
```powershell
.\run_assistant.bat
```
This script handles injecting custom signals, spins up Mosquitto and Kuksa Databroker Docker containers, and launches all microservices and feeders.
