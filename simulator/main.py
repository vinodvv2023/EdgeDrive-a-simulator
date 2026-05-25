import asyncio
import websockets
import json
import paho.mqtt.client as mqtt

# Initialize MQTT Client
try:
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
except AttributeError:
    mqtt_client = mqtt.Client()

# Physics state
speed = 0.0
rpm = 0
gear = 1
lat, lon = 40.7128, -74.0060

is_started = False
gas_pressed = False
brake_pressed = False

async def handle_client(websocket):
    global is_started, gas_pressed, brake_pressed, gear
    try:
        async for message in websocket:
            try:
                cmd = json.loads(message)
                action = cmd.get("action")
                val = cmd.get("value")
                
                if action == "start":
                    is_started = True
                elif action == "gas":
                    gas_pressed = val
                elif action == "brake":
                    brake_pressed = val
            except:
                pass
    except websockets.exceptions.ConnectionClosed:
        pass

async def simulate(websocket):
    global speed, rpm, gear, lat, lon, is_started, gas_pressed, brake_pressed
    
    # Run the handler task
    handler_task = asyncio.create_task(handle_client(websocket))
    
    print("Dashboard Connected to Simulator!")
    try:
        while True:
            if is_started:
                # Interactive Physics
                if gas_pressed:
                    speed += 4.0 * 0.1 # Acceleration
                elif brake_pressed:
                    speed -= 10.0 * 0.1 # Braking
                else:
                    speed -= 1.0 * 0.1 # Friction
                
                speed = max(0, min(240, speed))
                
                # Auto Shift Logic
                if speed < 20: gear = 1
                elif speed < 50: gear = 2
                elif speed < 80: gear = 3
                elif speed < 120: gear = 4
                elif speed < 160: gear = 5
                else: gear = 6
                
                # Simple RPM calculation
                max_speed_gear = gear * 40
                min_speed_gear = max(0, (gear - 1) * 40 - 20)
                
                if speed < min_speed_gear:
                    rpm = 800 # Idle
                else:
                    ratio = (speed - min_speed_gear) / (max_speed_gear - min_speed_gear)
                    rpm = 1000 + (ratio * 7000)
                    rpm = max(800, min(9000, rpm))
                    
                # GPS movement (rough line approximation)
                lat += 0.00001 * (speed / 100.0)
                lon += 0.00001 * (speed / 100.0)
                
            data = {
                "started": is_started,
                "speed": round(speed, 1),
                "rpm": round(rpm),
                "gear": gear,
                "lat": lat,
                "lon": lon,
                "pedals": {"gas": gas_pressed, "brake": brake_pressed}
            }
            
            print(f"CAN Sim Output: Speed: {data['speed']} KMPH | RPM: {data['rpm']} | Gear: {data['gear']} | Gas: {gas_pressed}")
            await websocket.send(json.dumps(data))
            
            # Publish via MQTT
            try:
                mqtt_client.publish("vehicle/simulator/telemetry", json.dumps(data))
            except Exception as e:
                print(f"MQTT Publish Error: {e}")
                
            await asyncio.sleep(0.1) # 10Hz
    except websockets.exceptions.ConnectionClosed:
        print("Client disconnected")
    finally:
        handler_task.cancel()

async def main():
    print("Connecting to MQTT Broker on localhost:1883...")
    try:
        mqtt_client.connect("127.0.0.1", 1883, 60)
        mqtt_client.loop_start()
        print("Connected to MQTT Broker!")
    except Exception as e:
        print(f"Warning: Could not connect to MQTT Broker: {e}")
        
    print("Starting CAN Simulator on ws://0.0.0.0:8083")
    async with websockets.serve(simulate, "0.0.0.0", 8083):
        await asyncio.Future()  # run forever

if __name__ == "__main__":
    asyncio.run(main())
