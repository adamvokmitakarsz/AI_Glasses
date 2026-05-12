//BLE Defines:

#define IMG_SERVICE_UUID    "0000abcd-0000-1000-8000-00805f9b34fb"
#define IMG_CONTROL_UUID    "00001234-0000-1000-8000-00805f9b34fb"
#define IMG_STATUS_UUID     "00001235-0000-1000-8000-00805f9b34fb"
#define IMG_DATA_UUID       "00001236-0000-1000-8000-00805f9b34fb"

#define CMD_SERVICE_UUID    "6d22fa7b-4f6c-4bd7-962c-a343c00060a1"
#define CMD_CMD_UUID        "06e025b3-597e-4c94-87df-c4bd1b4e0b0e"
#define CMD_BAT_UUID        "726530db-8845-4241-a10e-e26f20b095d6"

#define MTU_RATE 512
#define BUFFERSIZE 396

#define BLE_PASSKEY 123456

#define MAX_INDEX 9999

// Timeouts:
#define BLE_TIMEOUT     (200ULL)     //in s
#define TIME_TO_SLEEP   (30ULL)   // in s
#define REQUEST_TIMEOUT (2ULL) //timeout for img_state = SEND, if no commands are received for x amount of time, sends error, to "wake up" the client
