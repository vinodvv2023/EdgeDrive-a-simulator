from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import StreamingResponse
from pydantic import BaseModel
import cv2
import asyncio
import websockets
import json
import os
import winsound
import time
from threading import Thread
import paho.mqtt.client as mqtt

# ─── Load Telemetry Config ───────────────────────────────────────────────────
def _load_telemetry_cfg():
    cfg_path = os.path.join(os.path.dirname(__file__), "telemetry_config.json")
    try:
        with open(cfg_path, "r") as f:
            cfg = json.load(f)
        print(f"[OrchestratorCfg] Loaded telemetry_config.json ({len(cfg.get('fields', []))} fields)")
        return cfg
    except Exception as e:
        print(f"[OrchestratorCfg] WARNING: Could not load telemetry_config.json: {e}")
        return {"fields": []}

telemetry_config = _load_telemetry_cfg()

# Initialize MQTT Client
try:
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
except AttributeError:
    mqtt_client = mqtt.Client()

app = FastAPI()

# Camera Setup (Auto-detecting the first working webcam)
camera = None
for i in range(4):
    print(f"Trying to open camera index {i}...")
    cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
    if cap.isOpened():
        success, frame = cap.read()
        if success:
            print(f"Successfully connected to webcam at index {i}!")
            camera = cap
            break
        cap.release()

if camera is None:
    print("WARNING: Could not connect to any webcam.")

# Dashboard clients & command queue
dashboard_clients = []
to_simulator_queue = asyncio.Queue()

# VoiceEvent model and API endpoint to receive status updates from C++ Voice Assistant
class VoiceEvent(BaseModel):
    state: str
    text: str = ""
    response: str = ""

@app.post("/api/voice_event")
def post_voice_event(event: VoiceEvent):
    try:
        payload = json.dumps({
            "state": event.state,
            "text": event.text,
            "response": event.response
        })
        mqtt_client.publish("vehicle/voice_assistant/events", payload)
    except Exception as e:
        print(f"MQTT Publish Error (Orchestrator Voice Event): {e}")
    return {"status": "ok"}

@app.get("/api/vehicle_status")
def get_vehicle_status():
    global vehicle_state
    return vehicle_state

@app.get("/api/telemetry_config")
def get_telemetry_config():
    return telemetry_config

def generate_frames():
    if camera is None:
        while True:
            time.sleep(1)
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + b'' + b'\r\n')

    while True:
        success, frame = camera.read()
        if not success:
            time.sleep(0.1)
            continue
        ret, buffer = cv2.imencode('.jpg', frame)
        frame = buffer.tobytes()
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')

@app.get("/video_feed")
def video_feed():
    return StreamingResponse(generate_frames(), media_type="multipart/x-mixed-replace; boundary=frame")

@app.websocket("/ws/dashboard")
async def websocket_dashboard(websocket: WebSocket):
    await websocket.accept()
    dashboard_clients.append(websocket)
    try:
        while True:
            data = await websocket.receive_text()
            await to_simulator_queue.put(data)
    except WebSocketDisconnect:
        dashboard_clients.remove(websocket)

# Alarm state
is_over_120 = False
has_beeped_80 = False

# Global vehicle state cache for the Voice Assistant
vehicle_state = {
    "speed": 0.0,
    "rpm": 0,
    "gear": 1,
    "started": False,
    "speed_alarm": False,
    "fuel_level": 100.0,
    "coolant_temp": 40.0,
    "cooling_fan": False,
    "tyre_pressures": [32.0, 32.0, 32.0, 32.0]
}

# Per-field warning state: tracks whether TTS has already fired for each field key
_warn_fired = {}  # key -> bool

def send_warning_tts(text):
    import urllib.request
    import json
    try:
        url = "http://127.0.0.1:8081/speak"
        payload = {"text": text}
        req_data = json.dumps(payload).encode('utf-8')
        req = urllib.request.Request(url, data=req_data, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req) as response:
            pass
    except Exception as e:
        print(f"Failed to play warning TTS: {e}")

def send_welcome_tts():
    import urllib.request
    import json
    try:
        url = "http://127.0.0.1:8081/speak"
        payload = {"text": "Yes, the engine is currently running. welcome, Always maintain a safe distance from the vehicle in front of you."}
        req_data = json.dumps(payload).encode('utf-8')
        req = urllib.request.Request(url, data=req_data, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req) as response:
            pass
    except Exception as e:
        print(f"Failed to play welcome TTS: {e}")

def alarm_worker():
    global is_over_120
    while True:
        if is_over_120:
            winsound.Beep(1500, 500)
            time.sleep(0.5)
        else:
            time.sleep(0.1)

Thread(target=alarm_worker, daemon=True).start()

async def connect_to_simulator():
    global is_over_120, has_beeped_80
    uri = "ws://127.0.0.1:8083"
    last_started = False
    while True:
        try:
            async with websockets.connect(uri) as websocket:
                print("Connected to CAN Simulator.")
                
                async def send_to_sim():
                    while True:
                        msg = await to_simulator_queue.get()
                        await websocket.send(msg)
                
                send_task = asyncio.create_task(send_to_sim())
                
                try:
                    while True:
                        message = await websocket.recv()
                        data = json.loads(message)
                        speed = data.get("speed", 0)
                        fuel_level = float(data.get("fuel_level", 100.0))
                        coolant_temp = float(data.get("coolant_temp", 40.0))
                        cooling_fan = bool(data.get("cooling_fan", False))
                        tyre_pressures = data.get("tyre_pressures", [32.0, 32.0, 32.0, 32.0])
                        
                        # Detect engine start transition
                        started = bool(data.get("started", False))
                        if started and not last_started:
                            last_started = True
                            Thread(target=send_welcome_tts, daemon=True).start()
                        elif not started:
                            last_started = False
                        
                        out_str = f"Orchestrator Received: Speed: {speed} KMPH | RPM: {data.get('rpm')} | Gear: {data.get('gear')}"
                        
                        if speed >= 120:
                            is_over_120 = True
                            print(f"\033[91m{out_str}\033[0m")
                        elif speed >= 80:
                            if is_over_120: is_over_120 = False
                            print(f"\033[91m{out_str}\033[0m")
                            if not has_beeped_80:
                                Thread(target=lambda: winsound.Beep(1000, 500), daemon=True).start()
                                has_beeped_80 = True
                        else:
                            if is_over_120: is_over_120 = False
                            if has_beeped_80: has_beeped_80 = False
                            print(out_str)
                        
                        # Update global vehicle state cache
                        global vehicle_state
                        vehicle_state = {
                            "speed": float(speed),
                            "rpm": int(data.get("rpm", 0)),
                            "gear": int(data.get("gear", 1)),
                            "started": started,
                            "speed_alarm": is_over_120,
                            "fuel_level": fuel_level,
                            "coolant_temp": coolant_temp,
                            "cooling_fan": cooling_fan,
                            "tyre_pressures": tyre_pressures
                        }
                        
                        # ── Generic TTS threshold check driven by telemetry_config.json ──────
                        global _warn_fired
                        if started:
                            for field in telemetry_config.get("fields", []):
                                fkey    = field.get("key", "")
                                tts_msg = field.get("warn_tts_message", "")
                                if not tts_msg or not fkey:
                                    continue  # no TTS message configured for this field

                                if _warn_fired.get(fkey, False):
                                    continue  # already warned this session

                                raw_val = vehicle_state.get(fkey)
                                if raw_val is None:
                                    continue

                                triggered = False
                                warn_above = field.get("warn_above")
                                warn_below = field.get("warn_below")

                                if isinstance(raw_val, list):
                                    # Array field (e.g. tyre_pressures): warn if ANY element breaches
                                    if warn_below is not None:
                                        triggered = any(v <= warn_below for v in raw_val)
                                    if warn_above is not None:
                                        triggered = triggered or any(v >= warn_above for v in raw_val)
                                else:
                                    if warn_below is not None and float(raw_val) <= warn_below:
                                        triggered = True
                                    if warn_above is not None and float(raw_val) >= warn_above:
                                        triggered = True

                                if triggered:
                                    _warn_fired[fkey] = True
                                    msg = tts_msg  # capture for lambda
                                    Thread(target=lambda m=msg: send_warning_tts(m), daemon=True).start()
                        else:
                            # Engine stopped — reset all warning flags
                            _warn_fired = {}
                        
                        # Publish alarm state via MQTT
                        try:
                            mqtt_client.publish("vehicle/orchestrator/alarms", json.dumps({"speed_alarm": is_over_120}))
                        except Exception as e:
                            print(f"MQTT Publish Error (Orchestrator): {e}")
                        
                        for client in dashboard_clients.copy():
                            try:
                                await client.send_text(message)
                            except:
                                dashboard_clients.remove(client)
                finally:
                    send_task.cancel()
        except Exception as e:
            await asyncio.sleep(2)

@app.on_event("startup")
async def startup_event():
    print("Connecting Orchestrator to MQTT Broker on localhost:1883...")
    try:
        mqtt_client.connect("127.0.0.1", 1883, 60)
        mqtt_client.loop_start()
        print("Orchestrator connected to MQTT Broker!")
    except Exception as e:
        print(f"Warning: Orchestrator could not connect to MQTT Broker: {e}")
    asyncio.create_task(connect_to_simulator())

if __name__ == "__main__":
    import uvicorn
    print("Starting Orchestrator Server on port 8082...")
    uvicorn.run(app, host="0.0.0.0", port=8082)
