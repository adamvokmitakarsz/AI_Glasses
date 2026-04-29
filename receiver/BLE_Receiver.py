import asyncio
from bleak import BleakClient
import time

DEVICE_ADDRESS   = "90:70:69:11:EA:55"
CONTROL_CHAR_UUID = "00001234-0000-1000-8000-00805f9b34fb"
STATUS_CHAR_UUID  = "00001235-0000-1000-8000-00805f9b34fb"
DATA_CHAR_UUID    = "00001236-0000-1000-8000-00805f9b34fb"

state = None
size = None
expected_chunks = None
chunkcounter = 0
start = int()
result = bytearray()

def status_handler(sender: int, data: bytearray):
    global expected_chunks
    print("status changed!")
    state = chr(data[0])
    if(len(data) > 1):
        size = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4]
        expected_chunks = data[5] << 8 | data[6]
        print(f"command {state}, size {size}, chunks {expected_chunks}")
    else :
        print(f"command {state}")

def data_handler(sender, data: bytearray):
    #print(f"[DATA] from {sender} ")
    global chunkcounter, result, expected_chunks
    received_chunk_no = data[0] << 8 | data[1]
    #print(f"received chunk {chunckcounter} of {expected_chunks}: ")
    for i in range (2, len(data)):
        result.append(data[i]) 
    #print(f"received {received_chunk_no}")

    if chunkcounter != received_chunk_no:
        print(f"missed chunk {chunkcounter}")
    chunkcounter+=1
    if chunkcounter + 1 == expected_chunks:
        print("image received")


async def main():
    global result, start
    start = time.time()
    
    
    async with BleakClient("90:70:69:11:EA:55") as client:
        print("Connected!")
        
        await client.start_notify(STATUS_CHAR_UUID, status_handler)
        await client.start_notify(DATA_CHAR_UUID, data_handler)

        await client.write_gatt_char(CONTROL_CHAR_UUID, b'R', response=True)
        print("Written!")

        await asyncio.Event().wait()
if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        end = time.time()
        print("Stopped from keyboard...")
        with open ("file.jpg", "wb") as file:
            file.write(result)
        print(f"received {len(result)} bytes")
        print(f"program ran for {end - start} seconds")
