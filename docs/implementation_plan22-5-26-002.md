# Interactive Dashboard & Simulator Upgrade Plan

This plan addresses all five of your requests, transforming the dashboard from a passive display into a fully interactive driving simulator.

## 1. Wake Word Training ('Covesa')
Your project already contains the foundation for Keyword Spotting (KWS) using `sherpa-onnx`. 
To "train" or activate the wake word `Covesa`:
1. The file `encode_kws.py` already includes the line `encode("Covesa")`.
2. To compile this into the model's readable format, you simply need to run: `python encode_kws.py` in your terminal. This generates the `keywords.txt` file that the audio engine uses to recognize the wake word.
3. Your C++ Voice Assistant (`assistant_gui.cpp`) likely needs to be configured to load the Sherpa ONNX KWS engine using this `keywords.txt` file.

## User Review Required & Open Questions

> [!IMPORTANT]
> **Map Routing Approach**
> To show a start and end point route on the map, I propose drawing a pre-calculated GPS route (a polyline) on the map. The simulator will move the car marker along this exact path depending on your speed. Does a pre-calculated track (like a lap or a highway stretch) work for your needs?

> [!TIP]
> **Clutch/Gear Logic**
> In a real car, shifting requires holding the clutch. For the keyboard simulation: should pressing Left/Right Arrow *instantly* change the gear, or do you want a dedicated "Clutch" key (e.g., `C`) that must be held down while pressing Left/Right? I plan to make Left/Right instantly change gears to keep it simple, but let me know!

## Proposed Changes

### Dashboard UI (`dashboard/`)
#### [MODIFY] `src/App.jsx` & `src/index.css`
Rearrange the CSS Grid layout to match your specifications:
- **Top Center**: Pedal Indicators (Clutch, Brake, Gas) and Rear Web Cam (Dummy image/box)
- **Top Right**: Live PC Web Cam
- **Bottom Left**: Analog Tachometer (RPM)
- **Bottom Center**: Analog Speedometer
- **Bottom Right**: Maps

#### [NEW] `src/components/WifiGauge.jsx`
A completely custom SVG React component that draws curved bars (like a Wi-Fi signal). The number of illuminated bars and their color will dynamically change based on the current Speed or RPM.

#### [MODIFY] `src/components/MapView.jsx`
Add a polyline representing a simulated route (Start -> End). The car marker will travel along this line.

#### [MODIFY] Keyboard Event Listener
Add a global event listener in React that listens to:
- `S` key: Triggers the "Start Engine" sequence (intro video plays, UI appears).
- `ArrowUp` (Gas), `ArrowDown` (Brake), `ArrowLeft` (Gear Down), `ArrowRight` (Gear Up).
- These events will be sent via WebSocket to the Orchestrator.

### Orchestrator Microservice (`orchestrator/server.py`)
#### [MODIFY] `server.py`
Add bi-directional WebSocket logic. When the dashboard sends a keyboard command (e.g., `{"action": "gas_on"}`), the Orchestrator will immediately forward it to the CAN Simulator.

### CAN Simulator (`simulator/main.py`)
#### [MODIFY] `main.py`
Transform the physics engine from "auto-looping" to "interactive":
- Speed only increases when the `gas_on` command is active.
- Speed decreases sharply when `brake_on` is active, or slowly due to friction.
- Gear changes happen only upon receiving `gear_up` or `gear_down`.
- GPS coordinates are calculated to traverse the predefined map route based on the current speed.

## Verification Plan
1. **Manual Testing**: Start the services. The dashboard should remain dark until `S` is pressed.
2. **Video Sequence**: Verify the video plays upon start, then reveals the new rearranged dashboard.
3. **Interactive Physics**: Hold the `Up Arrow` and verify the Wi-Fi style speedometer bars light up. Let go, and verify speed drops.
4. **Map Movement**: Verify the vehicle marker moves along the route on the map when moving.
