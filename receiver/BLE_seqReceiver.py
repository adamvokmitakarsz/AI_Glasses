import asyncio
from bleak import BleakScanner, BleakClient, BleakError

SERVICE_UUID = "0000abcd-0000-1000-8000-00805f9b34fb"
CONTROL_UUID = "00001234-0000-1000-8000-00805f9b34fb"
STATUS_UUID  = "00001235-0000-1000-8000-00805f9b34fb"
DATA_UUID    = "00001236-0000-1000-8000-00805f9b34fb"

TARGET_NAME = "AIGLS"

image_counter = 0

# Image state
expected_chunks = 0
received_chunks = {}
image_size = 0


def reset_state():
    global expected_chunks, received_chunks, image_size
    expected_chunks = 0
    received_chunks = {}
    image_size = 0


def handle_status(_, data: bytearray):
    global expected_chunks, image_size

    if not data:
        return

    if data[0] == ord('S'):
        reset_state()

        image_size = int.from_bytes(data[1:5], 'big')
        expected_chunks = int.from_bytes(data[5:7], 'big')

        print(f"[STATUS] Start image: {image_size} bytes, {expected_chunks} chunks")

    elif data[0] == ord('E'):
        print("[STATUS] End of image")
        save_image()


def handle_data(_, data: bytearray):
    if len(data) < 2:
        return

    chunk_id = (data[0] << 8) | data[1]
    received_chunks[chunk_id] = data[2:]

    if expected_chunks > 0 and len(received_chunks) % 50 == 0:
        print(f"Received {len(received_chunks)}/{expected_chunks}")


def save_image():
    global image_counter

    if not received_chunks:
        print("No data received")
        return

    print("Reconstructing image...")

    ordered = []
    for i in range(expected_chunks):
        if i not in received_chunks:
            print(f"Missing chunk {i}, skipping image")
            return
        ordered.append(received_chunks[i])

    image_bytes = b''.join(ordered)

    filename = f"images/image_{image_counter:04d}.jpg"
    with open(filename, "wb") as f:
        f.write(image_bytes)

    print(f"Saved {filename}")

    image_counter += 1
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


async def connect_and_receive():
    while True:
        device = await find_device()

        try:
            async with BleakClient(device.address) as client:
                print("Connected!")

                await client.start_notify(STATUS_UUID, handle_status)
                await client.start_notify(DATA_UUID, handle_data)

                # Give ESP a moment after connection
                await asyncio.sleep(1)

                print("Requesting image...")
                await client.write_gatt_char(CONTROL_UUID, b'R')

                # Stay connected until ESP disconnects (sleep)
                while client.is_connected:
                    await asyncio.sleep(1)

                print("Disconnected (likely ESP sleep)")

        except BleakError as e:
            print(f"Connection error: {e}")

        # Wait before retrying (important)
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