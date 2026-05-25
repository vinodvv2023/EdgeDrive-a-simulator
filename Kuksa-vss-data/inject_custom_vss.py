import json
import os
import uuid

vss_in = r"c:\Users\xtrem\Downloads\CPlusPlus\CAN CTRL\Kuksa-vss-data\vss_rel_4.1_noexpand.json"
vss_out = r"c:\Users\xtrem\Downloads\CPlusPlus\CAN CTRL\Kuksa-vss-data\custom_vss.json"

if not os.path.exists(vss_in):
    print(f"Error: Input VSS file not found at {vss_in}")
    exit(1)

with open(vss_in, "r", encoding="utf-8") as f:
    data = json.load(f)

# Define Custom Voice Assistant branch under Vehicle.Cabin
voice_assistant_branch = {
    "description": "Voice Assistant Signals.",
    "type": "branch",
    "uuid": str(uuid.uuid4()),
    "children": {
        "State": {
            "datatype": "string",
            "description": "Current state of the Voice Assistant (IDLE, LISTENING, PROCESSING_STT, etc).",
            "type": "sensor",
            "uuid": str(uuid.uuid4())
        },
        "LastTranscribedText": {
            "datatype": "string",
            "description": "The last spoken query transcribed via Speech-to-Text.",
            "type": "sensor",
            "uuid": str(uuid.uuid4())
        },
        "LastResponse": {
            "datatype": "string",
            "description": "The last generated response spoken via Text-to-Speech.",
            "type": "sensor",
            "uuid": str(uuid.uuid4())
        }
    }
}

# Define Custom ADAS Cabin branch under Vehicle.ADAS
adas_cabin_branch = {
    "description": "Custom Cabin ADAS warning signals.",
    "type": "branch",
    "uuid": str(uuid.uuid4()),
    "children": {
        "IsSpeedAlarmActive": {
            "datatype": "boolean",
            "description": "Indicates if the high speed safety alarm (>120 km/h) is active.",
            "type": "sensor",
            "uuid": str(uuid.uuid4())
        }
    }
}

# Inject the branches
try:
    # 1. Inject under Cabin
    if "Cabin" in data["Vehicle"]["children"]:
        data["Vehicle"]["children"]["Cabin"]["children"]["VoiceAssistant"] = voice_assistant_branch
        print("Successfully injected custom VoiceAssistant signals under Vehicle.Cabin")
    else:
        print("Warning: Cabin branch not found, skipping VoiceAssistant injection.")

    # 2. Inject under ADAS
    if "ADAS" in data["Vehicle"]["children"]:
        data["Vehicle"]["children"]["ADAS"]["children"]["Cabin"] = adas_cabin_branch
        print("Successfully injected custom ADAS Cabin signals under Vehicle.ADAS")
    else:
        print("Warning: ADAS branch not found, skipping ADAS Cabin injection.")

    # Save to custom_vss.json
    with open(vss_out, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    print(f"Successfully generated custom VSS rules at: {vss_out}")

except KeyError as e:
    print(f"KeyError during VSS injection: {e}")
    exit(1)
