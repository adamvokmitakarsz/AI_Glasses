#include <Arduino.h>
#include "NimBLEDevice.h"
#include "esp_camera.h"
#include <SPI.h>
#include <SD.h>

//Config File
#include "config.h"

#pragma region BLE

//Variables and data for BLE communication 

 
uint8_t packet[BUFFERSIZE + 2]; //global, to make memory usage more obvious, might not make sense

NimBLEServer    *pServer;
NimBLEService   *pImgService;
NimBLECharacteristic *pImgControl;
NimBLECharacteristic *pImgStatus;
NimBLECharacteristic *pImgData;

NimBLEService   *pCmdService;
NimBLECharacteristic *pCmdChar;
NimBLECharacteristic *pBatChar;


bool client_connected = false;
bool data_request = false;

uint16_t current_conn_handle = BLE_HS_CONN_HANDLE_NONE;

RTC_DATA_ATTR uint32_t latest_index = 0;
uint32_t requested_image_index = 0;

//For CMDService
bool goToSleep = false;
bool reset = false;
uint32_t sleepTimeS = 0;

//BLE Callbacks
class ImgControlCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue val = pCharacteristic->getValue();
        Serial.print("Write to ImgControl from: ");
        Serial.println(connInfo.getAddress().toString().c_str());
        Serial.print("Data is:");
        Serial.write(val.data(), val.length());
        Serial.println();

        current_conn_handle = connInfo.getConnHandle();

        if (val.length() >= 5 && val.data()[0] == 'R') {
            uint32_t requestedIndex =
                (val.data()[1] << 24) |
                (val.data()[2] << 16) |
                (val.data()[3] << 8)  |
                (val.data()[4]);

            Serial.printf("Requested index: %lu\n", requestedIndex);

            requested_image_index = requestedIndex;
            data_request = true;
        } else {
            data_request = false;
        }
    }
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override{
        NimBLEAttValue val = pCharacteristic->getValue();
        Serial.print("Write to CMD from: ");
        Serial.println(connInfo.getAddress().toString().c_str());
        Serial.print("Data is:");
        Serial.write(val.data(), val.length());
        Serial.println();        

        if (val.length() >= 5 && val.data()[0] == 'S') {
            sleepTimeS =
                (val.data()[1] << 24) |
                (val.data()[2] << 16) |
                (val.data()[3] << 8)  |
                (val.data()[4]);
            goToSleep = true;
            Serial.printf("Sleep for: %lu s\n", sleepTimeS);
        }
        if( val.length() >= 1 && val.data()[0] == 'R'){
            reset = true;
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {

    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        //client_connected = false; // wait for auth before allowing data flow
        current_conn_handle = connInfo.getConnHandle();
        Serial.print("Connected (not yet authenticated): ");
        Serial.println(connInfo.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        client_connected = false;
        Serial.printf("Disconnected, reason: %d\n", reason);
        NimBLEDevice::startAdvertising();
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
        Serial.printf("MTU updated: %u\n", MTU);
    }

    // Called when pairing is complete
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        Serial.printf("Auth complete — encrypted: %d, authenticated: %d, bonded: %d\n",
            connInfo.isEncrypted(),
            connInfo.isAuthenticated(),
            connInfo.isBonded()
        );

        if (connInfo.isAuthenticated()) {   // don't require isAuthenticated() for Just Works
            Serial.println("Auth succes, allowing data flow");
            client_connected = true;
        } else {
            Serial.println("Auth failed — kicking client");
            NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
        }
    }

    // For DISPLAY_ONLY: called to get the passkey to show
    uint32_t onPassKeyDisplay() override {
        uint32_t passkey = NimBLEDevice::getSecurityPasskey();
        Serial.printf(">>> Passkey: %06lu <<<\n", passkey);
        return passkey;
    }
};

bool sendImage(uint8_t* image, uint size , uint index){
    uint8_t status[12];
    //uint8_t message[BUFFERSIZE + 2];
    uint16_t totalChunks = (size + BUFFERSIZE - 1) / BUFFERSIZE;
    Serial.print("Sending #");
    Serial.println(index);
    status[0] = 'S';
    
    //image index:
    status[1] = (index >> 24) & 0xFF;
    status[2] = (index >> 16) & 0xFF;
    status[3] = (index >> 8) & 0xFF;
    status[4] = index & 0xFF;
    //image size:
    status[5] = (size >> 24) & 0xFF;
    status[6] = (size >> 16) & 0xFF;
    status[7] = (size >> 8) & 0xFF;
    status[8] = (size) & 0xFF;
    //nmumber of chunks:
    status[9] = (totalChunks >> 8) & 0xFF;
    status[10]= (totalChunks) & 0xFF;
    status[11]= 0;

    pImgStatus->setValue(status, 11);
    pImgStatus->notify();

    int currentChunk;
    for (currentChunk = 0; currentChunk < totalChunks && client_connected; currentChunk++){
        int msgsize = ( (size - currentChunk*BUFFERSIZE)  < BUFFERSIZE ) ? (size - currentChunk*BUFFERSIZE) : (BUFFERSIZE);

        packet[0] = (currentChunk >> 8) & 0xFF;
        packet[1] = currentChunk & 0xFF; 
        memcpy(packet+2, image + BUFFERSIZE*currentChunk, msgsize);
        pImgData->setValue(packet, msgsize + 2 );
        pImgData->notify();
        
        delay(20); //10 ms works with my windows pc w/ intel AX200, receiving using python
    }

    if (currentChunk == totalChunks){
        Serial.println("Image sent");
    } else {
        Serial.println("Image NOT sent");
    }
    pImgStatus->setValue('E');
    pImgStatus->notify();

    //delay(150); // seems like the last chunk keeps goig missing, so i added a longer delay here 

    return currentChunk == totalChunks;
}

void sendNotAvailable(uint32_t index, uint32_t last_available) {
    uint8_t msg[9];
    msg[0] = 'N';

    msg[1] = (index >> 24) & 0xFF;
    msg[2] = (index >> 16) & 0xFF;
    msg[3] = (index >> 8) & 0xFF;
    msg[4] = index & 0xFF;
    
    msg[5] = (last_available >> 24) & 0xFF;
    msg[6] = (last_available >> 16) & 0xFF;
    msg[7] = (last_available >> 8) & 0xFF;
    msg[8] = last_available & 0xFF;

    pImgStatus->setValue(msg, 9);
    pImgStatus->notify();
    
}

void sendError(uint32_t index, uint8_t code) {
    uint8_t msg[6];
    msg[0] = 'X';

    msg[1] = (index >> 24) & 0xFF;
    msg[2] = (index >> 16) & 0xFF;
    msg[3] = (index >> 8) & 0xFF;
    msg[4] = index & 0xFF;

    msg[5] = code;

    pImgStatus->setValue(msg, 6);
    pImgStatus->notify();
    
}

void sendBattery(uint8_t batteryPercent){
    pBatChar->setValue(batteryPercent);
    pBatChar->notify();
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
        .frame_size     = FRAMESIZE_XGA,
        .jpeg_quality   = 5,
        .fb_count       = 2,
        .fb_location    = CAMERA_FB_IN_PSRAM,
        .grab_mode      = CAMERA_GRAB_LATEST
    };

//bool took_picture=false;
camera_fb_t * framebuffer;


#pragma endregion CAMERA


#pragma region SD


#define SD_CS 21

bool exists_SD = 0;

uint32_t findLatestIndex() {
    uint32_t maxIndex = 0;
    File root = SD.open("/");

    if(!root || !root.isDirectory()) {
        Serial.println("Failed to open root");
        return 0;
    }

    File file = root.openNextFile();
    while(file) {
        if(!file.isDirectory()) {
            String name = file.name();
            name = "/" + name;
            if(name.endsWith(".jpg")) {
                int slash = name.lastIndexOf('/');
                int dot   = name.lastIndexOf('.');
                if(slash >= 0 && dot > slash) {
                    String numStr = name.substring(slash + 1, dot);
                    uint32_t index = numStr.toInt();
                    if(index > maxIndex) {
                        maxIndex = index;
                    }
                }
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    return maxIndex;
}

void deleteAllImages() {

    File root = SD.open("/");
    if(!root || !root.isDirectory()) {
        Serial.println("Failed to open SD root");
        return;
    }

    File file = root.openNextFile();
    while(file) {
        String path = file.name();
        file.close();
        path = "/" + path;
        Serial.print("Deleting ");
        Serial.println(path);

        if(!SD.remove(path)) {
            Serial.println("Delete failed");
        }

        file = root.openNextFile();
    }
    latest_index = 0;
    Serial.println("Done deleting");
}

#pragma endregion SD


#pragma region MAIN
//some time things, in s

inline unsigned long toMicros(unsigned long sec){ 
    return sec * 1000000UL;
}

uint8_t getBattery(){
    //dummy function
    //TODO: implement proper battery management and calculation
    return 16;
}



unsigned long start_time;

enum ImgState{
    TAKE_PICTURE,
    WAIT_FOR_CONNECTION,
    SEND,
    SAVE_TO_SD,
    GO_SLEEP
} img_state;


void setup() {
    start_time = micros();
    img_state = TAKE_PICTURE;
    //BLE Setup
    Serial.begin(115200);
    delay(100);

    ///BLE setup:
    Serial.print("Initializing nimBLE....");
    NimBLEDevice::init("AIGLS");
    NimBLEDevice::setMTU(MTU_RATE);
    pServer = NimBLEDevice::createServer(); //create server
    pImgService = pServer->createService(IMG_SERVICE_UUID); //create image service
    
    pImgControl = pImgService->createCharacteristic(IMG_CONTROL_UUID, NIMBLE_PROPERTY::WRITE ); //command characteristic
    pImgStatus  = pImgService->createCharacteristic(IMG_STATUS_UUID,  NIMBLE_PROPERTY::NOTIFY); //status characteristic
    pImgData    = pImgService->createCharacteristic(IMG_DATA_UUID,    NIMBLE_PROPERTY::NOTIFY); //data characteristic
    
    pCmdService = pServer->createService(CMD_SERVICE_UUID); //create control + battery service
    
    pCmdChar    = pCmdService->createCharacteristic(CMD_CMD_UUID, NIMBLE_PROPERTY::WRITE );
    pBatChar    = pCmdService->createCharacteristic(CMD_BAT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY );


    pServer->setCallbacks(new ServerCallbacks);
    pImgControl->setCallbacks(new ImgControlCallbacks);
    pCmdChar->setCallbacks(new CmdCallbacks);

    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(BLE_PASSKEY);

    pServer->start();

    pImgControl->setValue("Hello BLE");
    pImgStatus->setValue(0);
    pImgData->setValue(0);
    
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(IMG_SERVICE_UUID); // advertise the UUID of our service
    pAdvertising->setName("AIGLS"); // advertise the device name
    pAdvertising->start();

    Serial.println("DONE");
    
    ///Camera setup:
    
    Serial.println("Initializing Camera");
    if(esp_camera_init( &camera_config) != ESP_OK){ // this works so far
        Serial.println("Camera init error!");
    }
    delay(5000);
    
    ///SD setup:
    Serial.println("Initializing SD:");
    exists_SD = SD.begin(21);
    // if(exists_SD){
    //     latest_index = findLatestIndex();
    // }
    
    
}

uint64_t sendStartTime = 0;

void loop(){

    static int capture_attempts = 0;
    static int send_attempts = 0;
    char imgPath[32];

    if(reset){
        Serial.println("Received RESET");

        pServer->disconnect(current_conn_handle);
        delay(200);
        deleteAllImages();
        esp_restart();
    }
    
    if(goToSleep){
        Serial.print("Received command SLEEP, sleeping for: ");
        Serial.println(sleepTimeS);

        pServer->disconnect(current_conn_handle);
        delay(200);
        esp_camera_deinit();
        esp_sleep_enable_timer_wakeup(toMicros(sleepTimeS));
        esp_deep_sleep_start();
    }

    switch(img_state){
        case(TAKE_PICTURE) : {
            //digitalWrite(LED_BUILTIN, HIGH);
            Serial.println("taking picture");
            framebuffer = esp_camera_fb_get();
            
            capture_attempts++;
            
            if(!framebuffer){
                Serial.print("couldnt take picture, attempt: ");
                Serial.println(capture_attempts);
                img_state = TAKE_PICTURE;
            } else {
                capture_attempts = 0;
                img_state = WAIT_FOR_CONNECTION;
                latest_index = (latest_index + 1) % MAX_INDEX; // circular thing
            }
            
            break;
        }
        case(WAIT_FOR_CONNECTION):{
            if(client_connected) {
                    img_state = SEND;
                    sendStartTime = micros();
                    sendBattery(getBattery());
                    break;
            }

            if(micros() - start_time > toMicros(BLE_TIMEOUT) ){ 
                Serial.println("BLE Connection Timed out...");
                img_state = SAVE_TO_SD;
            }
            
            break;
        }
        
        case(SEND) : {

            if( requested_image_index - 1 < latest_index && data_request ){ //delete previous image: if next is already requesting => there was no error
                Serial.println("Deleting previous");
                sprintf(imgPath, "/%04d.jpg", requested_image_index - 1);
                if(SD.exists(imgPath)) {
                    SD.remove(imgPath);
                }
            }

            if( requested_image_index < latest_index && data_request ){
                Serial.println("Sending from SD");
                //char imgPath[32];
                sprintf(imgPath, "/%04d.jpg", requested_image_index);
                if(!SD.exists(imgPath)){
                    sendError(requested_image_index, 0);
                    data_request = false;
                    break;
                }
                fs::File file = SD.open(imgPath, "r", false);
                int fileSize = file.size();
                uint8_t* image;
                image = (uint8_t *) ps_malloc(fileSize);
                if(image == NULL){
                    sendError(requested_image_index, 1);
                    data_request = false;
                    file.close();
                    break;
                }
                file.read(image, fileSize); // loads whole image into buffer, not too efficient, but works for now
                sendImage(image, fileSize, requested_image_index);
                data_request = false;
                free(image);
                file.close();
                
                break;
            }
            if( requested_image_index == latest_index && data_request){
                sendImage(framebuffer->buf, framebuffer->len, requested_image_index);
                break;
            }
            if( requested_image_index > latest_index && data_request) {
                sendNotAvailable(requested_image_index, latest_index);
                data_request = false;
                if( requested_image_index == latest_index + 1){
                    img_state = GO_SLEEP;
                }
                break;
            }
            if ( !client_connected ||  ( micros() - sendStartTime > toMicros(REQUEST_TIMEOUT) )  ){
                img_state = WAIT_FOR_CONNECTION;
                sendError(latest_index, 5); //"yo something timed out!"
            }
            break;

        }
        case(SAVE_TO_SD):{
            Serial.println("Saving to SD Card");
            //TODO: implement saving here

            //char imgPath[10];
            sprintf(imgPath, "/%04d.jpg", latest_index);
            fs::File file = SD.open(imgPath, "w", true);
            file.write(framebuffer->buf, framebuffer->len);
            file.close();

            img_state = GO_SLEEP;
            break;
        }
        case(GO_SLEEP):{
            Serial.println("freeing memory and going to sleep");
            esp_camera_fb_return(framebuffer);
            delay(50);

            //digitalWrite(LED_BUILTIN, LOW);
            unsigned long now = micros();
            if( ( now - start_time ) < toMicros(TIME_TO_SLEEP) ) { // go to sleep for a non-negative amount of time
                uint64_t timeToSleep = toMicros(TIME_TO_SLEEP) - ( now - start_time );

                pServer->disconnect(current_conn_handle);
                delay(200);
                esp_camera_deinit();
                esp_sleep_enable_timer_wakeup(timeToSleep);
                esp_deep_sleep_start();

            } else { // if too much time has passed, start from beginning again.
                img_state = TAKE_PICTURE;
                start_time = micros();
            }
            break;
        }
    }
}

#pragma endregion MAIN