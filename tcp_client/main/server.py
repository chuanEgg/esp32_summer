import socket
import json
import threading
import time
import argparse
import logging

# Setup logger
logging.basicConfig(
    level=logging.INFO,
    format="[%(levelname)s] %(message)s"
)
logger = logging.getLogger("tcp_server")

clients = {}
clients_lock = threading.Lock()


def day_micro():
    return int(time.time() % 86400 * 1_000_000)
    # as a notice, if you test lightdance over midnight, you have to run it again.


def handle_client(client, addr):
    buffer = ""
    while True:
        try:
            data = client.recv(1024).decode()
            if not data:
                break
            buffer += data
            while '\n' in buffer:
                line, buffer = buffer.split('\n', 1)
                try:
                    msg = json.loads(line)
                except Exception as e:
                    logger.error(f"JSON decode error from {addr}: {e}")
                    continue
                if msg.get("type") == "sync":
                    t_1 = msg.get("t_1")
                    t_2 = day_micro()
                    sync_resp = {
                        "type": "sync_resp",
                        "t_2": t_2,
                        "t_3": day_micro()
                    }
                    client.sendall((json.dumps(sync_resp) + '\n').encode())
                    logger.info(
                        f"sync from {addr}, t_1={t_1}, t_2={t_2}, t_3={sync_resp['t_3']}")

                # ...handle other message types if needed...
        except Exception as e:
            logger.error(f"client {addr} error: {e}")
            break


def receiveClient(server):
    while True:
        try:
            client, addr = server.accept()  # accept a client
            with clients_lock:
                clients[addr[0]] = client
            logger.info(f"current clients: {len(clients)}")
        except Exception as e:
            logger.error(f"Error {e}")
            break

        logger.info(f"start client handler for {addr}")
        threading.Thread(target=handle_client, args=(
            client, addr), daemon=True).start()


def parse_input(user_input, addr=None) -> tuple | None:
    # split user_input and divide cmd as well as args
    tokens = user_input.split()
    if not tokens:
        return None
    cmd = tokens[0]
    args = []

    # Restrict play command to: play <seconds>
    # Use match-case (Python 3.10+) for command parsing
    match cmd:
        case "play":
            if len(tokens) != 2:
                logger.warning("Usage: play <seconds>")
                return None
            try:
                seconds = int(tokens[1])
            except ValueError:
                logger.warning("Invalid seconds value")
                return None

            # Inform clients to sync before play
            sync_msg = {
                "type": "sync"
            }
            sync_json = json.dumps(sync_msg) + '\n'
            with clients_lock:
                for c in clients.values():
                    try:
                        c.sendall(sync_json.encode())
                    except Exception as e:
                        logger.error(f"failed to send sync due to {e}")

            time.sleep(0.1)  # Wait 100ms for clients to process sync

            args = ["playerctl", "play", str(
                seconds * 1000 * 1000)]  # microseconds
            return {
                "type": "command",
                "args": args,
                "t_send": day_micro()
            }, addr
        case "pause" | "stop" | "restart" | "quit":
            args = ["playerctl", cmd]
        case "list":
            args = ["list"]

            return {
                "type": "check",
                "args": args
            }, addr
        case "ledtest":
            args = ["ledtest"] + tokens[1:]
        case "oftest":
            args = ["oftest"] + tokens[1:]
        case _:
            logger.warning("Not support")
            return None

    return {
        "type": "command",
        "args": args
    }, addr


def main(args):
    HOST = args.host
    PORT = args.port

    server = socket.socket(
        socket.AF_INET, socket.SOCK_STREAM)  # socket.socket()
    server.bind((HOST, PORT))  # .bind(IPaddress, port)
    server.listen(30)  # start to listen, .listen(max amount clients in line)
    logger.info(f"listening on {server.getsockname()[0]}")
    client_thread = threading.Thread(
        target=receiveClient, args=(server,), daemon=True)
    client_thread.start()

    while True:
        try:
            user_input = input("SERVER: Enter:")  # get userinput
            # if input is exit or quit, terminate the program

            if user_input.strip().lower() in ["exit", "quit"]:
                logger.info("quit")
                break

            if ":" in user_input:
                cmd, addr = user_input.split(":", 1)
                addr = addr.strip()
                cmd = cmd.strip()
                logger.info(f"command for {addr}: {cmd}")
                command = parse_input(cmd, addr)
            else:
                command = parse_input(user_input)

            # turn it into json style
            if command is not None:
                command, addr = command
                if command.get("type") == "check":
                    # sometimes, not all user input is going to be remote command
                    if command["args"][0] == "list":
                        # list all connected clients
                        for addr, client in clients.items():
                            print(f"{addr}, {client}")
                else:
                    json_str = json.dumps(command) + '\n'
                    with clients_lock:
                        if addr is not None:
                            try:
                                clients[addr].sendall(json_str.encode())
                            except Exception as e:
                                logger.error(
                                    f"failed to send json to {addr} due to {e}")
                        else:
                            for c in clients.values():
                                try:
                                    c.sendall(json_str.encode())
                                    logger.info("send json")
                                except Exception as e:
                                    logger.error(f"failed to send due to {e}")

        except KeyboardInterrupt:
            break
        except Exception as e:
            logger.error(f"error {e}")
            break

    with clients_lock:
        for c in clients.values():
            c.close()
    server.close()
    logger.info("finish connection")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="TCP Server", add_help=False)
    parser.add_argument("--host", default="0.0.0.0", help="Host to bind")
    parser.add_argument("--port", type=int, default=5000,
                        help="Port to listen")
    parser.add_argument("--debug", action="store_true",
                        help="Enable debug mode")
    parser.add_argument("--help", action="store_true",
                        help="Show help message")
    args = parser.parse_args()

    if args.help:
        print("""
    TCP Server Help:

    This server listens for TCP connections from clients and allows you to send commands to them.
    Usage:
      python server.py [--host HOST] [--port PORT] [--debug] [--help]

    Options:
      --host      Host/IP to bind to (default: 0.0.0.0)
      --port      Port to listen on (default: 5000)
      --debug     Enable debug logging
      --help      Show this help message

    Commands (enter at SERVER: prompt):
      play <seconds>      Start playback for <seconds> seconds (sends sync first)
      pause               Pause playback
      stop                Stop playback
      restart             Restart playback
      list                List all connected clients
      ledtest [...]       Run LED test with optional arguments
      oftest [...]        Run OF test with optional arguments
      quit/exit           Quit the server

    You can also target a specific client by IP:
      <command>:<client_ip>
    Example:
      play 10:192.168.1.42

    Press Ctrl+C to exit at any time.
              """)
        exit(0)
    logging.basicConfig(level=logging.DEBUG if args.debug else logging.INFO)
    main(args)
