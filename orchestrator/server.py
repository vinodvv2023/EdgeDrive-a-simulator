from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import StreamingResponse
import cv2
import asyncio
import websockets
import json
import winsound
import time
from threading import Thread
import paho.mqtt.client as mqtt

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
