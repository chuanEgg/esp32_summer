import socket
import time
import json
import contextlib


def setup_network():
    """Setup network configuration and return local IP and broadcast IP"""
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
        # Default broadcast address - can be changed by user input
        broadcast_ip = f"{ip_parts[0]}.{ip_parts[1]}.{ip_parts[2]}.255"
        print(f"[INFO] Local IP: {local_ip}")
        print(f"[INFO] Default broadcast IP: {broadcast_ip}")
        return local_ip, broadcast_ip
    except Exception as e:
        local_ip = "127.0.0.1"
        broadcast_ip = "255.255.255.255"
        print(f"[INFO] Using fallback IPs due to error: {e}")
        return local_ip, broadcast_ip


def send_command_and_wait_ack(target_ip, cmd, target_devices, local_ip):
    """Send command to target IP and wait for ACKs"""
    START_PORT = 12345
    ACK_PORT = 3333
    session_id = int(time.time())  # Use timestamp as session ID

    # Create broadcast socket
    broadcast_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    broadcast_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    broadcast_sock.bind(('', 0))

    try:
        # Send commands to devices
        for seq, target in enumerate(target_devices):
            msg = {
                "target": target,
                "cmd": cmd,
                "seq": seq,
                "delay_ms": 10000,
                "session": session_id,
                "ack_ip": local_ip,
                "ack_port": ACK_PORT
            }
            data = json.dumps(msg).encode('utf-8')
            broadcast_sock.sendto(data, (target_ip, START_PORT))
            time.sleep(0.1)  # Small delay between packets
            print(f"[Sent] {msg} to {target_ip}:{START_PORT}")

        # Wait for ACKs from devices
        expected_ids = set(target_devices)

        ack_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ack_sock.bind(("", ACK_PORT))
        ack_sock.settimeout(3)

        received_acks = set()
        start_time = time.time()

        print("[Server] Waiting for ACKs...")
        while time.time() - start_time < 3:
            try:
                data, addr = ack_sock.recvfrom(1024)
                message = json.loads(data.decode())
                print(f"[ACK] From {addr}: {message}")
                device_id = message.get("id")
                if device_id:
                    received_acks.add(device_id)

                # Break if all ACKs received
                if received_acks == expected_ids:
                    print("[OK] All ACKs received.")
                    break

            except socket.timeout:
                print("[Server] No more ACKs received, timeout.")
                break

        # Check if all expected ACKs were received
        missing = expected_ids - received_acks
        if missing:
            print(f"[WARN] Missing ACKs from: {missing}")

        ack_sock.close()

    finally:
        broadcast_sock.close()


def main():
    """Main loop for interactive command sending"""
    print("=== ESP32 UDP Command Server ===")
    local_ip, default_broadcast_ip = setup_network()

    print("\nCommands: 'blink', 'stop', 'start', etc")
    print("Target devices: ESP32_A, ESP32_B (comma-separated)")
    print("Press Ctrl+C to exit\n")

    try:
        while True:
            print("-" * 50)
            # Get target IP from user
            target_ip_input = input(
                f"Enter target IP (default: {default_broadcast_ip}): ").strip()
            if not target_ip_input:
                target_ip = default_broadcast_ip
            else:
                target_ip = target_ip_input

            # Get command from user
            cmd = input("Enter command: ").strip()
            if not cmd:
                print("[WARN] Empty command, skipping...")
                continue

            # Get target devices
            devices_input = input(
                "Enter target devices (default: ESP32_A,ESP32_B): ").strip()
            if not devices_input:
                target_devices = ['ESP32_A', 'ESP32_B']
            else:
                target_devices = [device.strip()
                                  for device in devices_input.split(',')]

            print(
                f"\n[INFO] Sending '{cmd}' to {target_devices} at {target_ip}")
            send_command_and_wait_ack(target_ip, cmd, target_devices, local_ip)

    except KeyboardInterrupt:
        print("\n[INFO] Server stopped by user.")
    except Exception as e:
        print(f"[ERROR] Unexpected error: {e}")


if __name__ == "__main__":
    main()
