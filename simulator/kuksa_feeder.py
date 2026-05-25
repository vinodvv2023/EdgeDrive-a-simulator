import asyncio
import json
import logging
import time
import random
import paho.mqtt.client as mqtt
from kuksa_client.grpc.aio import VSSClient
from kuksa_client.grpc import Datapoint

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger(__name__)

# Config
MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
KUKSA_HOST = "127.0.0.1"
KUKSA_PORT = 55555

# Topics
TELEMETRY_TOPIC = "vehicle/simulator/telemetry"
ALARM_TOPIC = "vehicle/orchestrator/alarms"
VOICE_TOPIC = "vehicle/voice_assistant/events"

# State tracking for Event-Driven Smart Logging
state = {
    "speed": None,
    "rpm": None,
    "gear": None,
    "started": None,
    "lat": None,
    "lon": None,
    "gas": None,
    "brake": None,
    "speed_alarm": None,
    "last_heartbeat_time": 0.0
}

# Queue to share MQTT messages with the asyncio loop
mqtt_queue = asyncio.Queue()

# MQTT Callbacks
def on_connect(client, userdata, flags, rc, properties=None):
    logger.info("Connected to MQTT Broker successfully!")
    client.subscribe([(TELEMETRY_TOPIC, 0), (ALARM_TOPIC, 0), (VOICE_TOPIC, 0)])
    logger.info(f"Subscribed to topics: {TELEMETRY_TOPIC}, {ALARM_TOPIC}, {VOICE_TOPIC}")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
        # Put topic and payload tuple into queue
        asyncio.run_coroutine_threadsafe(mqtt_queue.put((msg.topic, payload)), loop)
    except Exception as e:
        logger.error(f"Error queuing MQTT message: {e}")

# Async loop handler for MQTT events
async def process_telemetry(kuksa_client):
    while True:
        topic, payload = await mqtt_queue.get()
        current_time = time.time()
        updates = {}

        if topic == TELEMETRY_TOPIC:
            # Parse simulator payload
            started = bool(payload.get("started", False))
            speed = float(payload.get("speed", 0.0))
            rpm = int(payload.get("rpm", 0))
            gear = int(payload.get("gear", 1))
            lat = float(payload.get("lat", 0.0))
            lon = float(payload.get("lon", 0.0))
            pedals = payload.get("pedals", {})
            gas = bool(pedals.get("gas", False))
            brake = bool(pedals.get("brake", False))

            # Smart Filtering Logic
            is_heartbeat = (current_time - state["last_heartbeat_time"]) >= 15.0
            
            # 1. Speed (Delta threshold > 3.0 km/h or Heartbeat)
            if state["speed"] is None or abs(speed - state["speed"]) >= 3.0 or is_heartbeat:
                updates["Vehicle.Speed"] = Datapoint(speed)
                state["speed"] = speed
                logger.info(f"[Smart Log] Speed update triggered: {speed} km/h (Delta or Heartbeat)")

            # 2. Engine RPM (Delta threshold > 200 rpm or Heartbeat)
            if state["rpm"] is None or abs(rpm - state["rpm"]) >= 200 or is_heartbeat:
                updates["Vehicle.Powertrain.CombustionEngine.Speed"] = Datapoint(rpm)
                state["rpm"] = rpm

            # 3. Engine Running State (Immediate trigger on state change)
            if state["started"] != started:
                updates["Vehicle.Powertrain.CombustionEngine.IsRunning"] = Datapoint(started)
                state["started"] = started
                logger.info(f"[Smart Log] Engine state changed to: {started}")

            # 4. Gear (Immediate trigger on state change)
            if state["gear"] != gear:
                updates["Vehicle.Powertrain.Transmission.CurrentGear"] = Datapoint(gear)
                state["gear"] = gear
                logger.info(f"[Smart Log] Transmission Gear changed to: {gear}")

            # 5. Pedals (Immediate trigger on state change)
            if state["gas"] != gas:
                updates["Vehicle.Chassis.Accelerator.PedalPosition"] = Datapoint(100 if gas else 0)
                state["gas"] = gas
            if state["brake"] != brake:
                updates["Vehicle.Chassis.Brake.PedalPosition"] = Datapoint(100 if brake else 0)
                state["brake"] = brake

            # 6. GPS Location (Heartbeat or significant change)
            if state["lat"] is None or is_heartbeat or abs(lat - state["lat"]) > 0.0001:
                updates["Vehicle.CurrentLocation.Latitude"] = Datapoint(lat)
                updates["Vehicle.CurrentLocation.Longitude"] = Datapoint(lon)
                state["lat"] = lat
                state["lon"] = lon

            if is_heartbeat:
                state["last_heartbeat_time"] = current_time

        elif topic == ALARM_TOPIC:
            # Parse speed safety alarm payload from Orchestrator
            speed_alarm = bool(payload.get("speed_alarm", False))
            
            # 7. Speed Alarm (Immediate trigger on state change)
            if state["speed_alarm"] != speed_alarm:
                updates["Vehicle.ADAS.Cabin.IsSpeedAlarmActive"] = Datapoint(speed_alarm)
                state["speed_alarm"] = speed_alarm
                logger.info(f"[Smart Log] Custom VSS: ADAS Speed Alarm changed to: {speed_alarm}")

        elif topic == VOICE_TOPIC:
            # Parse voice event from C++ Voice Assistant via Orchestrator relay
            state_val = str(payload.get("state", "IDLE"))
            text_val = str(payload.get("text", ""))
            response_val = str(payload.get("response", ""))
            
            updates["Vehicle.Cabin.VoiceAssistant.State"] = Datapoint(state_val)
            if text_val:
                updates["Vehicle.Cabin.VoiceAssistant.LastTranscribedText"] = Datapoint(text_val)
            if response_val:
                updates["Vehicle.Cabin.VoiceAssistant.LastResponse"] = Datapoint(response_val)
                
            logger.info(f"[Smart Log] Custom VSS: Voice Assistant State changed to: {state_val}")

        # Send batch updates to Kuksa Databroker
        if updates:
            try:
                await kuksa_client.set_current_values(updates)
            except Exception as e:
                logger.error(f"Failed to update Kuksa: {e}")

        mqtt_queue.task_done()

async def main():
    global loop
    loop = asyncio.get_running_loop()

    # Initialize MQTT client
    try:
        mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except AttributeError:
        mqtt_client = mqtt.Client()
        
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    logger.info(f"Connecting to MQTT Broker at {MQTT_BROKER}:{MQTT_PORT}...")
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        logger.critical(f"Failed to connect to MQTT Broker: {e}")
        return

    # Keep reconnecting to Kuksa Databroker
    while True:
        try:
            logger.info(f"Connecting to Kuksa Databroker at grpc://{KUKSA_HOST}:{KUKSA_PORT}...")
            async with VSSClient(KUKSA_HOST, KUKSA_PORT) as kuksa_client:
                await process_telemetry(kuksa_client)
        except Exception as e:
            logger.error(f"Kuksa connection error: {e}. Reconnecting in 3 seconds...")
            await asyncio.sleep(3)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Feeder script terminated.")
