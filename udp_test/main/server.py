import socket
import time
import json
import contextlib

# socket.AF_INET = IPv4, socket.SOCK_DGRAM = UDP
broadcast_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Enable broadcast mode
broadcast_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
# Bind to all interfaces to allow broadcasting
broadcast_sock.bind(('', 0))

# Try to get the local network broadcast address, fallback to 255.255.255.255
try:
    # Get local IP to determine broadcast address
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as temp_sock:
        temp_sock.connect(("8.8.8.8", 80))
        # 8.8.8.8 is a public DNS server, used to determine the local IP
        # This avoids sending packets to the internet, which is unnecessary for local broadcasts
        local_ip = temp_sock.getsockname()[0]

    # For most local networks, use the local broadcast (e.g., 192.168.1.255)
    ip_parts = local_ip.split('.')
    # you can just change the broadcast address to target specific client
    # how is this a good implementation?
    BROADCAST_IP = f"{ip_parts[0]}.{ip_parts[1]}.{ip_parts[2]}.246"
    print(f"[INFO] Using broadcast IP: {BROADCAST_IP}")
except Exception as e:
    BROADCAST_IP = "255.255.255.255"
    print(
        f"[INFO] Using fallback broadcast IP: {BROADCAST_IP} due to error: {e}")

START_PORT = 12345
ACK_PORT = 3333
session_id = 42

# Send start commands to devices
for seq, target in enumerate(['ESP32_A', 'ESP32_B']):
    msg = {
        "target": target,
        "cmd": "blink",
        "seq": seq,
        "delay_ms": 10000,
        "session": session_id,
        "ack_ip": local_ip,
        "ack_port": ACK_PORT
    }
    data = json.dumps(msg).encode('utf-8')
    broadcast_sock.sendto(data, (BROADCAST_IP, START_PORT)
                          )  # sendto: send UDP packet
    time.sleep(3)  # space packets by 50ms
    print(f"[Sent] {msg} to {BROADCAST_IP}:{START_PORT}")


# Wait for ACKs from devices
# the datatype is a set for fast membership testing
EXPECTED_IDS = {"ESP32_A", "ESP32_B"}

ack_sock = socket.socket(
    socket.AF_INET, socket.SOCK_DGRAM)  # create UDP socket
ack_sock.bind(("", ACK_PORT))  # bind to all interfaces on port 3333
ack_sock.settimeout(3)  # timeout after 0.5 seconds if no data

received_acks = set()
start_time = time.time()

print("[Server] Waiting for ACKs...")
while time.time() - start_time < 3:
    try:
        data, addr = ack_sock.recvfrom(1024)
        # recvfrom: receive UDP packet and sender address, 1024 is the buffer size
        message = json.loads(data.decode())
        print(f"[ACK] From {addr}: {message}")
        device_id = message.get("id")
        if device_id:
            received_acks.add(device_id)
    except socket.timeout:
        print("[Server] No more ACKs received, exiting wait loop.")
        break  # exit loop if timeout


# Check if all expected ACKs were received
missing = EXPECTED_IDS - received_acks
if missing:
    print(f"[WARN] Missing ACKs from: {missing}")
    stop_msg = {
        "cmd": "stop",
        "reason": "missing_acks",
        "session": session_id
    }
    stop_data = json.dumps(stop_msg).encode()
    stop_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    stop_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    stop_sock.sendto(stop_data, (BROADCAST_IP, START_PORT)
                     )  # broadcast stop command
else:
    print("[OK] All ACKs received.")

# Close sockets to release resources
broadcast_sock.close()
ack_sock.close()
