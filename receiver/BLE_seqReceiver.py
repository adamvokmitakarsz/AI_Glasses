import asyncio
from bleak import BleakScanner, BleakClient, BleakError

SERVICE_UUID = "0000abcd-0000-1000-8000-00805f9b34fb"
CONTROL_UUID = "00001234-0000-1000-8000-00805f9b34fb"
STATUS_UUID  = "00001235-0000-1000-8000-00805f9b34fb"
DATA_UUID    = "00001236-0000-1000-8000-00805f9b34fb"

CMD_CMD_UUID = "06e025b3-597e-4c94-87df-c4bd1b4e0b0e"
CMD_BAT_UUID = "726530db-8845-4241-a10e-e26f20b095d6"


TARGET_NAME = "AIGLS"

image_counter = 1


# Image state
expected_chunks = 0
received_chunks = {}
image_size = 0
image_attempts = 0

receiving = False
waiting_for_response = False

def reset_state():
    global expected_chunks, received_chunks, image_size, image_attempts
    expected_chunks = 0
    received_chunks = {}
    image_size = 0
    image_attempts = 0

def handle_status(_, data):
    global image_counter
    global expected_chunks
    global receiving
    global waiting_for_response

    if not data:
        return

    msg_type = chr(data[0])

    if msg_type == 'S':
        reset_state()

        index = int.from_bytes(data[1:5], 'big')
        size = int.from_bytes(data[5:9], 'big')
        chunks = int.from_bytes(data[9:11], 'big')

        expected_chunks = chunks
        receiving = True

        print(f"Receiving image {index}")

    elif msg_type == 'E':
        print("Image complete")

        save_image()

        image_counter += 1

        receiving = False
        waiting_for_response = False

    elif msg_type == 'N':
        index = int.from_bytes(data[1:5], 'big')
        last_available = int.from_bytes(data[5:9], 'big')

        print(f"Image {index} not ready, last one was: {last_available}")

        if(last_available + 1 < index): #check if gone way over
            image_counter = last_available 
        # DON'T increment
        receiving = False
        waiting_for_response = False

    elif msg_type == 'X':
        index = int.from_bytes(data[1:5], 'big')
        print(f"Error on image {index}, err: {data[5]}")
        if data[5] != 5: 
            image_counter += 1
        receiving = False
        waiting_for_response = False



def handle_data(_, data: bytearray):
    global received_chunks, expected_chunks
    if len(data) < 2:
        return

    chunk_id = (data[0] << 8) | data[1]
    received_chunks[chunk_id] = data[2:]

    if expected_chunks > 0 and len(received_chunks) % 50 == 0:
        print(f"Received {len(received_chunks)}/{expected_chunks}")

async def request_image(client):
    global image_counter
    global waiting_for_response

    msg = b'R' + image_counter.to_bytes(4, 'big')
    await client.write_gatt_char(CONTROL_UUID, msg)
    waiting_for_response = True
    print(f"Requested image {image_counter}")


def save_image():
    global image_counter, image_attempts

    if not received_chunks:
        print("No data received")
        return

    print("Reconstructing image...")

    ordered = []
    for i in range(expected_chunks):
        if i not in received_chunks:
            print(f"Missing chunk {i}")
            if image_attempts < 3 : 
                print("attempting again...")
                image_counter -= 1
                image_attempts += 1
            return
        ordered.append(received_chunks[i])

    image_bytes = b''.join(ordered)

    filename = f"images/image_{image_counter:04d}.jpg"
    with open(filename, "wb") as f:
        f.write(image_bytes)

    print(f"Saved {filename}")

    
    reset_state()


async def find_device():
    while True:
        print(f"Scanning for {TARGET_NAME}...")
        devices = await BleakScanner.discover(timeout=3.0)

        for d in devices:
            if d.name == TARGET_NAME:
                print(f"Found {TARGET_NAME}: {d.address}")
                return d

        await asyncio.sleep(1)  # avoid hammering scan

def handle_battery(_, data: bytearray):
    percentage = int.from_bytes(data)
    print(f"Battery percentage is {percentage}")


async def terminal_input_loop(client):
    loop = asyncio.get_running_loop()

    while client.is_connected:

        try:
            # non-blocking terminal input
            cmd = await loop.run_in_executor(None, input, "> ")

            cmd = cmd.strip()

            valid = False

            # Accept plain R
            if cmd == "R":
                valid = True
                await client.write_gatt_char( CMD_CMD_UUID, b'R' )

            # Accept S<number>
            elif len(cmd) >= 2 and cmd[0] == 'S' and cmd[1:].isdigit():
                num = int(cmd[1:])
                payload = b'S' + num.to_bytes(4, 'big')
                await client.write_gatt_char( CMD_CMD_UUID, payload )
                valid = True

            if valid:
                print(f"Sent CMD: {cmd}")

            else:
                print("Invalid command")

        except Exception as e:
            print(f"Terminal input error: {e}")
            return


async def connect_and_receive():
    while True:
        device = await find_device()

        try:
            async with BleakClient(device.address) as client:
                print("Connected")

                # The rest of your connection logic is unchanged
                terminal_task = asyncio.create_task(terminal_input_loop(client))
                await client.start_notify(STATUS_UUID, handle_status)
                await client.start_notify(DATA_UUID, handle_data)
                await client.start_notify(CMD_BAT_UUID, handle_battery)

                await asyncio.sleep(1)

                while client.is_connected:
                    if not waiting_for_response:
                        await request_image(client)
                    await asyncio.sleep(0.2)

                terminal_task.cancel()
                print("Disconnected")

        except BleakError as e:
            print(f"Connection/pairing error: {e}")

        await asyncio.sleep(2)


async def main():
    while True:
        try:
            await connect_and_receive()
        except Exception as e:
            print(f"Fatal error: {e}")
            await asyncio.sleep(5)


if __name__ == "__main__":
    asyncio.run(main())