# Note: written using ChatGPT for now
I haven't noticed any mistakes (yet), but take everything here with a grain of salt.
Might rewrite later, depending if I have the time

# ESP32 BLE Image Transfer Protocol Documentation

## Overview

This project implements a low-power BLE image transfer system:

* The ESP32 acts as a BLE server and image producer
* A client (mobile app, Python script, etc.) connects periodically
* The ESP32 captures a JPEG image every wake cycle (~1 minute)
* The client requests images by index
* Images are transferred in BLE notification chunks
* The ESP32 may store missed images on SD card
* The ESP32 enters deep sleep between captures

The current reference client is implemented in Python using Bleak.

This document is intended primarily for developers implementing:

* Android clients
* iOS clients
* Flutter/React Native BLE clients
* Desktop BLE clients

---

# Architecture

## ESP32 Behavior

Lifecycle:

1. Wake from deep sleep
2. Initialize BLE + camera
3. Capture JPEG image
4. Advertise BLE service
5. Wait for client connection
6. Serve requested image(s)
7. Save unsent images to SD card if needed
8. Go back to sleep

---

# BLE Services

The ESP32 exposes **two BLE services**.

---

# 1. Image Transfer Service

Service UUID:

```text
0000abcd-0000-1000-8000-00805f9b34fb
```

Purpose:

* Image requests
* Transfer status
* Binary image chunks

---

## Characteristics

### Image Control Characteristic

UUID:

```text
00001234-0000-1000-8000-00805f9b34fb
```

Properties:

* READ
* WRITE
* WRITE WITHOUT RESPONSE

Purpose:

* Client requests an image by index

---

### Image Status Characteristic

UUID:

```text
00001235-0000-1000-8000-00805f9b34fb
```

Properties:

* NOTIFY

Purpose:

* Transfer state
* Errors
* Metadata
* Completion messages

---

### Image Data Characteristic

UUID:

```text
00001236-0000-1000-8000-00805f9b34fb
```

Properties:

* NOTIFY

Purpose:

* Raw JPEG chunk transfer

---

# 2. Command Service

Service UUID:

```text
6d22fa7b-4f6c-4bd7-962c-a343c00060a1
```

Purpose:

* Device commands
* Battery reporting

---

## Characteristics

### Command Characteristic

UUID:

```text
06e025b3-597e-4c94-87df-c4bd1b4e0b0e
```

Properties:

* WRITE

Purpose:

* Reset device
* Force sleep

---

### Battery Characteristic

UUID:

```text
726530db-8845-4241-a10e-e26f20b095d6
```

Properties:

* READ
* NOTIFY

Purpose:

* Battery percentage updates

Current implementation returns a dummy value.

---

# BLE MTU and Packet Size

The ESP32 requests:

```cpp
MTU = 512
```

Actual negotiated MTU depends on the client platform.

Image chunks use:

```cpp
BUFFERSIZE = 396 bytes
```

Each BLE image packet contains:

| Bytes | Description                     |
| ----- | ------------------------------- |
| 0-1   | Chunk index (uint16 big-endian) |
| 2-N   | JPEG data                       |

Maximum payload:

```text
398 bytes total
```

---

# Image Request Protocol

## Requesting an Image

The client writes to:

```text
IMG_CONTROL_UUID
```

Payload format:

| Byte(s) | Description                     |
| ------- | ------------------------------- |
| 0       | ASCII 'R'                       |
| 1-4     | Image index (uint32 big-endian) |

Example:

```python
b'R' + image_index.to_bytes(4, 'big')
```

Request image #42:

```text
52 00 00 00 2A
```

---

# Image Status Messages

All status messages are sent via notifications on:

```text
IMG_STATUS_UUID
```

---

## 1. Start Transfer ('S')

Sent before image chunks begin.

Payload:

| Byte(s) | Description         |
| ------- | ------------------- |
| 0       | ASCII 'S'           |
| 1-4     | Image index         |
| 5-8     | Image size in bytes |
| 9-10    | Total chunk count   |

Example:

```text
[S][index][size][chunks]
```

Client should:

* Reset transfer state
* Allocate buffers if desired
* Begin collecting chunks

---

## 2. End Transfer ('E')

Sent after all chunks were transmitted.

Payload:

| Byte(s) | Description |
| ------- | ----------- |
| 0       | ASCII 'E'   |

Client should:

* Verify all chunks received
* Reassemble image
* Save image
* Request next image

---

## 3. Not Available ('N')

Sent when requested image does not exist yet.

Payload:

| Byte(s) | Description                  |
| ------- | ---------------------------- |
| 0       | ASCII 'N'                    |
| 1-4     | Requested image index        |
| 5-8     | Latest available image index |

Meaning:

* Requested image is newer than what ESP32 currently has

Typical case:

* Client requested future image before capture completed

---

## 4. Error ('X')

Payload:

| Byte(s) | Description |
| ------- | ----------- |
| 0       | ASCII 'X'   |
| 1-4     | Image index |
| 5       | Error code  |

Known error codes:

| Code | Meaning                  |
| ---- | ------------------------ |
| 0    | SD file missing          |
| 1    | Memory allocation failed |
| 5    | Timeout/disconnect       |

---

# Image Data Packets

Sent through:

```text
IMG_DATA_UUID
```

Format:

| Byte(s) | Description                     |
| ------- | ------------------------------- |
| 0-1     | Chunk index (uint16 big-endian) |
| 2-N     | JPEG bytes                      |

Example:

```text
[00 05][JPEG DATA...]
```

means:

* Chunk #5

---

# Image Reconstruction

Client algorithm:

1. Receive `'S'`
2. Store:

   * image index
   * image size
   * expected chunk count
3. Receive data notifications
4. Map chunks by chunk ID
5. Receive `'E'`
6. Verify all chunks exist
7. Concatenate chunks in order
8. Save JPEG

Pseudo-code:

```pseudo
onStatus(S):
    clear chunk map
    expectedChunks = parsed chunks

onData(packet):
    chunkId = first 2 bytes
    data = remaining bytes
    chunks[chunkId] = data

onStatus(E):
    for i in range(expectedChunks):
        ensure chunk exists

    image = concat(chunks in order)
    save(image)
```

---

# Recommended Mobile App Flow

## Connection Flow

1. Scan for device name:

   ```text
   AIGLS
   ```

2. Connect

3. Discover services

4. Enable notifications for:

   * IMG_STATUS_UUID
   * IMG_DATA_UUID
   * CMD_BAT_UUID

5. Negotiate highest MTU possible

6. Wait briefly (~500-1000ms)

7. Request image index

---

# Important Timing Notes

The ESP32:

* Sleeps aggressively
* Disconnects before deep sleep
* May only advertise for ~10 seconds
* Uses notification pacing (`delay(20)`)

Mobile apps should:

* Reconnect automatically
* Handle disconnects gracefully
* Persist image index locally
* Retry missing images

---

# Recommended Client State Machine

```text
DISCONNECTED
    ↓
SCANNING
    ↓
CONNECTED
    ↓
SUBSCRIBED
    ↓
REQUEST_IMAGE
    ↓
RECEIVING
    ↓
VERIFY_IMAGE
    ↓
SAVE_IMAGE
    ↓
REQUEST_NEXT
```

---

# Handling Packet Loss

BLE notifications are not guaranteed delivery.

Current protocol reliability strategy:

* Chunk numbering
* Client-side verification
* Retry by requesting same image again

Recommended improvements for production:

* ACK/NACK mechanism
* CRC checksum
* Per-chunk retransmission
* Windowed transfer

---

# Battery Characteristic

Battery notifications are sent whenever the client connects.

Format:

```text
uint8 percentage
```

Current implementation:

```cpp
return 16;
```

This is placeholder logic.

---

# Command Protocol

Commands are written to:

```text
CMD_CMD_UUID
```

---

## Reset Command

Payload:

```text
'R'
```

Effect:

* Disconnect client
* Clear SD card
* Restart ESP32

---

## Sleep Command

Payload:

| Byte(s) | Description               |
| ------- | ------------------------- |
| 0       | ASCII 'S'                 |
| 1-4     | Sleep duration in seconds |

Example:
Sleep for 300 seconds:

```python
b'S' + (300).to_bytes(4, 'big')
```

---

# Storage Behavior

If no client connects before timeout:

* Image is saved to SD card
* File format:

```text
/0001.jpg
/0002.jpg
...
```

Previously missed images may later be requested.

---

# Important Mobile App Considerations

## Android

Recommended libraries:

* Kotlin:

  * Nordic BLE Library
  * Android BLE APIs
* Flutter:

  * flutter_blue_plus

Important:

* Request large MTU immediately
* Use foreground service for long-running BLE
* Handle Android BLE instability carefully

---

## iOS

Important:

* CoreBluetooth does not expose MTU directly
* Notification throughput may vary
* Background BLE behavior is restricted

Recommendations:

* Persist incomplete transfers
* Expect disconnects during sleep

---

# Known Limitations

Current implementation limitations:

* No encryption/authentication
* No transfer checksum
* No chunk retransmission
* Full SD image loaded into RAM
* Uses notification timing delays
* Single-client only
* No flow control

---

# Suggested Future Improvements

## Protocol

* Add CRC32 validation
* Add ACK packets
* Add resumable transfers
* Add metadata messages
* Add version negotiation

---

## Performance

* Stream directly from SD
* Dynamic chunk sizing
* Connection parameter tuning
* L2CAP CoC support

---

## Reliability

* Retransmission requests
* Chunk bitmaps
* Timeout recovery
* Better disconnect handling

---

# Example Client Flow

Example timeline:

```text
Client connects
↓
Subscribe notifications
↓
Request image #1
↓
ESP sends 'S'
↓
ESP sends chunks 0..N
↓
ESP sends 'E'
↓
Client reconstructs JPEG
↓
Client saves image
↓
Client requests image #2
↓
ESP may sleep/disconnect
↓
Client reconnects later
```

---

# Reference Python Client

The provided Python script demonstrates:

* Device discovery
* BLE connection
* Notification handling
* Chunk reassembly
* JPEG saving
* Reconnect handling
* Simple command interface

It is intended as:

* A protocol reference
* A debugging tool
* A basis for mobile implementation

---

# Suggested README Structure for Repository

```text
README.md
/docs
    protocol.md
    mobile-client-guide.md
    troubleshooting.md
    architecture.md
/examples
    python-client/
    android/
    ios/
```

---

# Troubleshooting

## Missing Final Chunk

Symptoms:

* JPEG corrupted
* Missing chunk at end

Cause:

* BLE notification pacing too aggressive

Current mitigation:

```cpp
delay(20);
```

Potential fixes:

* Increase delay
* Reduce chunk size
* Add acknowledgements

---

## Cannot Connect

Check:

* ESP32 advertising window
* BLE permissions
* MTU negotiation
* Device sleep timing

---

## Corrupted Images

Check:

* Missing chunk IDs
* Incomplete chunk map
* Notification subscription timing

---

# Summary

This project implements a compact BLE image transfer protocol optimized for:

* Low-power ESP32 operation
* Periodic image capture
* Simple client implementation
* Mobile compatibility

The protocol is intentionally lightweight and easy to implement on:

* Android
* iOS
* Flutter
* React Native
* Desktop BLE environments
