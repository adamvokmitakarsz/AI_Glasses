#include <Arduino.h>
#include "NimBLEDevice.h"
#include "esp_camera.h"

#pragma region BLE

//Variables and data for BLE communication 
#define SERVICE_UUID    "0000abcd-0000-1000-8000-00805f9b34fb"
#define CONTROL_UUID    "00001234-0000-1000-8000-00805f9b34fb"
#define STATUS_UUID     "00001235-0000-1000-8000-00805f9b34fb"
#define DATA_UUID       "00001236-0000-1000-8000-00805f9b34fb"
#define MTU_RATE 512
#define BUFFERSIZE 396
 
uint8_t packet[BUFFERSIZE]; //TODO: implement this as buffer

NimBLEServer    *pServer;
NimBLEService   *pService;
NimBLECharacteristic *pControl;
NimBLECharacteristic *pStatus;
NimBLECharacteristic *pData;

bool client_connected = false;
bool data_request = false;
enum State {
    IDLE

};
bool done = 0;
//BLE Callbacks
class ControlCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue val = pCharacteristic->getValue();
        Serial.print("Write from: ");
        Serial.println(connInfo.getAddress().toString().c_str());
        Serial.print("Data is:");
        Serial.write(val.data(), val.length());
        Serial.println();

        if (pCharacteristic->getValue().c_str()[0] == 'R'){
            data_request = true;
            done = 0;
        } else {
            data_request = false;
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        client_connected = true;
        
        Serial.print("Connected: ");
        Serial.println(connInfo.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        client_connected = false;

        Serial.print("Disconnected: ");
        Serial.println(connInfo.getAddress().toString().c_str());
        NimBLEDevice::startAdvertising();
    }
    
    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override { 
       Serial.printf("MTU updated: %u for connection ID: %u\n", MTU, connInfo.getConnHandle());
    }

};

void sendImage(uint8_t* image, uint size ){
    uint8_t status[8];
    uint8_t message[BUFFERSIZE + 2];
    uint16_t totalChunks = (size + BUFFERSIZE - 1) / BUFFERSIZE;
    Serial.println("sending status message");
    status[0] = 'S';
    status[1] = (size >> 24) & 0xFF;
    status[2] = (size >> 16) & 0xFF;
    status[3] = (size >> 8) & 0xFF;
    status[4] = (size) & 0xFF;

    status[5] = (totalChunks >> 8) & 0xFF;
    status[6] = (totalChunks) & 0xFF;
    status[7] = 0;

    pStatus->setValue(status, 7);
    pStatus->notify();

    int currentChunk;
    for (currentChunk = 0; currentChunk < totalChunks && client_connected; currentChunk++){
        int msgsize = ( (size - currentChunk*BUFFERSIZE)  < BUFFERSIZE ) ? (size - currentChunk*BUFFERSIZE) : (BUFFERSIZE);

        // Serial.print("C[");
        // Serial.print(i);
        // Serial.print("]/");
        // Serial.println(totalChunks);
        message[0] = (currentChunk >> 8) & 0xFF;
        message[1] = currentChunk & 0xFF; 
        memcpy(message+2, image + BUFFERSIZE*currentChunk, msgsize);
        pData->setValue(message, msgsize + 2 );
        pData->notify();
        
        delay(20); //10 ms works with my windows pc w/ intel AX200, receiving using python
    }

    if (currentChunk == totalChunks){
        Serial.println("Image sent");
    } else {
        Serial.println("Image NOT sent");
    }
    pStatus->setValue('E');
    pStatus->notify();

}

#pragma endregion BLE

#pragma region CAMERA

static camera_config_t camera_config = {
        .pin_pwdn       = -1,
        .pin_reset      = -1,
        .pin_xclk       = 10,
        .pin_sccb_sda   = 40,
        .pin_sccb_scl   = 39,
        .pin_d7         = 48,
        .pin_d6         = 11,
        .pin_d5         = 12,
        .pin_d4         = 14,
        .pin_d3         = 16,
        .pin_d2         = 18,
        .pin_d1         = 17,
        .pin_d0         = 15,
        .pin_vsync      = 38,
        .pin_href       = 47,
        .pin_pclk       = 13,

        .xclk_freq_hz   = 20000000,
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,
        .pixel_format   = PIXFORMAT_JPEG,
        .frame_size     = FRAMESIZE_SXGA,
        .jpeg_quality   = 4,
        .fb_count       = 1,
        .fb_location    = CAMERA_FB_IN_PSRAM,
        .grab_mode      = CAMERA_GRAB_WHEN_EMPTY
    };



bool took_picture=false;
camera_fb_t * framebuffer;

#pragma endregion CAMERA

#pragma region MAIN

void setup() {
    //BLE Setup
    Serial.begin(115200);
    delay(100);

    Serial.print("Initializing nimBLE....");
    NimBLEDevice::init("NimBLE");
    NimBLEDevice::setMTU(MTU_RATE);
    pServer = NimBLEDevice::createServer(); //create server
    pService = pServer->createService(SERVICE_UUID); //create service
    
    pControl = pService->createCharacteristic(CONTROL_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR); //command characteristic
    pStatus  = pService->createCharacteristic(STATUS_UUID, NIMBLE_PROPERTY::NOTIFY); //status characteristic
    pData    = pService->createCharacteristic(DATA_UUID, NIMBLE_PROPERTY::NOTIFY);  //data characteristic
    
    pServer->setCallbacks(new ServerCallbacks);
    pControl->setCallbacks(new ControlCallbacks);

    
    pServer->start();

    pControl->setValue("Hello BLE");
    pStatus->setValue(0);
    pData->setValue(0);
    
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID); // advertise the UUID of our service
    pAdvertising->setName("NimBLE"); // advertise the device name
    pAdvertising->start();

    Serial.println("DONE");
    
    
    Serial.println("Initializing Camera");
    if(esp_camera_init( &camera_config) != ESP_OK){ // this works so far
        while(1)
            Serial.println("Camera init error!");
    }
    delay(5000);
    Serial.println("taking picture");
    framebuffer = esp_camera_fb_get();
    
    if(!framebuffer){
        while(1)
            Serial.println("couldnt take picture!");
            delay(100);
    } else {
        took_picture = true;
    }

}

void loop(){
    if(took_picture){
        if(client_connected && data_request && !done){
            sendImage(framebuffer->buf, framebuffer->len);
            done = true;
        }
    } else {
        Serial.println("malloc!!!");
    }
}

#pragma endregion MAIN