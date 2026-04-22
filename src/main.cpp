#include <Arduino.h>
#include "NimBLEDevice.h"

#define DATASIZE 200000
uint8_t * randomData = NULL;

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
        
        delay(15); //10 ms works with my windows pc w/ intel AX200, receiving using python
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

#pragma region MAIN

bool malloc_work=false;
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

    randomData = (uint8_t *) ps_malloc(DATASIZE);
    if(randomData!=NULL){
        malloc_work = true;    
        for(int i = 0; i < DATASIZE; i++){
            //randomData[i] = 'A';// + rand()%( 'Z' - 'A' );
            randomData[i] = '0' + i%10;
        }
    }


}

void loop(){
    if(malloc_work){
        if(client_connected && data_request && !done){
            sendImage(randomData, DATASIZE);
            //Serial.println("sent image");
            done = true;
        }
    } else {
        Serial.println("malloc!!!");
    }
}

#pragma endregion MAIN