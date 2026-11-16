#include "gps_external.hpp"
#include "driver/uart.h"
#include <cstring>

// Create instance of HardwareSerial class for Serial 2 
HardwareSerial gpsExternalSerial(2);

ubxFrame_t * getUbxConfigFrame(ubx_config_data_key key, uint8_t * data, uint16_t size) {
    // Setup config message to set UART1 protocol to UBX
    ubxFrame_t * configFrame = new ubxFrame_t{};
    configFrame->prea1 = UBX_PREA_1;
    configFrame->prea2 = UBX_PREA_2;    
    configFrame->msgIdentifiers[0] = UBX_CFG_VALSET_MSG_CLASS;
    configFrame->msgIdentifiers[1] = UBX_CFG_VALSET_MSG_ID;
    configFrame->length = 8 + size; // 4 + 4 <key size> + <value size>
    configFrame->payload = new uint8_t[configFrame->length];

    // Setup config data
    configFrame->payload[0] = 0x00; // Version
    configFrame->payload[1] = 0b001; // Update configuration in the RAM layer
    configFrame->payload[2] = 0; // Reserved byte, must be set to 0
    configFrame->payload[3] = 0; // Reserved byte, must be set to 0
    memcpy(configFrame->payload + 4, &key, 4);
    memcpy(configFrame->payload + 8, data, size);

    // Calculate and set checksum
    uint8_t * checksums = calcChecksums(configFrame);
    configFrame->ckA = checksums[0];
    configFrame->ckB = checksums[1];

    delete[] checksums;

    return configFrame;
}

void sendMsgToGPS(ubxFrame_t * frame) {
    // Copy frame to buffer to send
    size_t buffSize = 8 + frame->length;
    uint8_t * buff = new uint8_t[buffSize];
    memcpy(buff, &frame->prea1, 1);
    memcpy(buff + 1, &frame->prea2, 1);
    memcpy(buff + 2, frame->msgIdentifiers, 2);
    memcpy(buff + 4, &frame->length, 2);
    memcpy(buff + 6, frame->payload, frame->length);
    memcpy(buff + 6 + frame->length, &frame->ckA, 1);
    memcpy(buff + 6 + frame->length + 1, &frame->ckB, 1);

    // Ensure all bytes of data are sent to GPS
    size_t bytesSent = 0;
    do {
        bytesSent += gpsExternalSerial.write(buff + bytesSent, buffSize - bytesSent);
    } while(bytesSent < 8 + frame->length); 

    // Cleanup 
    delete[] buff;
}

bool configGpsExternal() {
    // Set up frames to send
    uint8_t frameData[3] = {CFG_UART1OUTPROT_UBX_ENA, CFG_MSGOUT_UBX_NAV_PVT_UART1_ENA, CFG_UART1OUTPROT_NMEA_DISABLE};
    ubxFrame_t * enableUbxFrame = getUbxConfigFrame(CFG_UART1OUTPROT_UBX_KEY, &frameData[0], 1);
    ubxFrame_t * enableNavPvtFrame = getUbxConfigFrame(CFG_MSGOUT_UBX_NAV_PVT_UART1_KEY, &frameData[1], 1);
    ubxFrame_t * disableNmeaFrame = getUbxConfigFrame(CFG_UART1OUTPROT_NMEA_KEY, &frameData[2], 1);
    ubxFrame_t * messages[] = {enableUbxFrame, enableNavPvtFrame, disableNmeaFrame};
    size_t msgCnt = 3;
    ubx_ack_msg ackMsg;
    bool success = true;

    // For each frame:
    // 1. Send message to sensor
    // 2. Wait for a response (ack or nak)
    //   - If nak send again and signal that nak happened
    //   - If ack, move to next msg to send
    //   - If no response, send msg again and signal no response happened
    for(int i = 0; i < msgCnt; i++) {
        int retryCnt = 0;
        do {
            sendMsgToGPS(messages[i]);
            ackMsg = getUbxAck(UBX_CFG_VALSET_MSG_CLASS, UBX_CFG_VALSET_MSG_ID);
            switch(ackMsg) {
                case UBX_ACK_TIMEOUT:
                    Serial.println("Ack timed out. Sending same message again...");
                    retryCnt++;
                    break;
                case UBX_NAK_RECEIVED:
                    Serial.println("NAK received. Sending same message again...");
                    retryCnt++;
                    break;
                case UBX_ACK_RECEIVED:
                    Serial.println("ACK received. Move to next message");
                    break;
            }
        } while(ackMsg != UBX_ACK_RECEIVED && retryCnt < 3);

        if(retryCnt >= 3) {
            Serial.println("Failure to configure UBX UART");
            success = false;
            break;
        }
    }

    // Cleanup pointers
    for(int i = 0; i < msgCnt; i++) {
        delete messages[i];
    }

    if (success) Serial.println("UBX UART configuration complete");
    return success;
}

void startGpsExternal() {
    if(!gpsExternalSerial) {
        gpsExternalSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
    }
}

void stopGpsExternal() {
    if(gpsExternalSerial) {
        gpsExternalSerial.end();
    }
}

void resetFrame(ubxFrame_t * frame) {
    std::memset(frame->msgIdentifiers, 0, 2);
    frame->length = 0;
    if(frame->payload != nullptr) {
        delete[] frame->payload;
        frame->payload = nullptr;
    }
    frame->ckA = 0;
    frame->ckB = 0;
}

bool isChecksumsValid(ubxFrame_t * frame) {
    bool validChecksums = true;
    uint8_t * checksums = calcChecksums(frame);
    if(!(frame->ckA == checksums[0] && frame->ckB == checksums[1])) {
        validChecksums = false;
    }
    delete[] checksums;
    return validChecksums;
}

uint8_t * calcChecksums(ubxFrame_t * frame) {
    int checksumBuffSize = 4 + frame->length; 
    uint8_t * checksumBuff = new uint8_t[checksumBuffSize]; // Calculate checksum using bits from message class to end of payload
    
    // Copy frame data into checksumBuff (NOTE: can optimize by calculating checksum while receiving bytes instead of allocating another buff to calc checksum here)
    std::memcpy(checksumBuff, &frame->msgIdentifiers, 2);
    std::memcpy(&checksumBuff[2], &frame->length, 2);
    std::memcpy(&checksumBuff[4], frame->payload, frame->length);

    uint8_t calcCkA = 0;
    uint8_t calcCkB = 0;
    for(int i = 0; i < checksumBuffSize; i++) { // Loop for calculating checksum from section 3.4 "UBX checksum" in datasheet
        calcCkA = calcCkA + checksumBuff[i];
        calcCkB = calcCkB + calcCkA;
    }

    uint8_t * checksums = new uint8_t[2];
    checksums[0] = calcCkA;
    checksums[1] = calcCkB;

    delete[] checksumBuff;

    return checksums;
}

ubxFrame_t * getFrameFromExternalGps(int maxTotalTime) {
    uartState currState = WAIT_PREAMBLE_1; // State of frame transmission
    int buffIndx = 0; // Current index in buff
    uint8_t incomingBuff; // Buffer read from UART line
    uint8_t * tempBuf = new uint8_t[2];  // Temp buff for receiving length

    ubxFrame_t * frame = new ubxFrame_t; // Frame received
    frame->isTimeout = false;
    frame->payload = nullptr;

    unsigned long startTime = millis();
    unsigned long lastReadTime = millis();
    while (currState != PROCESS_FRAME) { // Run until frame ready to be processed OR until processesing times out
        if(millis() - lastReadTime >= READ_TIMEOUT || millis() - startTime >= maxTotalTime) {
            frame->isTimeout = true;
            break;
        } else if(gpsExternalSerial.available() > 0) {
            incomingBuff = gpsExternalSerial.read();

            switch(currState) {
                case WAIT_PREAMBLE_1:
                    if(incomingBuff == UBX_PREA_1) {
                        buffIndx = 1;
                        currState = WAIT_PREAMBLE_2;
                    }
                    break;
                case WAIT_PREAMBLE_2:
                    if(incomingBuff == UBX_PREA_2) { // If second preamble received, start recording message identifiers
                        buffIndx = 2;
                        currState = GET_MESSAGE_IDENTIFIERS;
                    } else if(incomingBuff != UBX_PREA_1) { // If neither first or second preamble arrived, go back to waiting for first preamble
                        buffIndx = 0;
                        currState = WAIT_PREAMBLE_1;
                    }
                    break;
                case GET_MESSAGE_IDENTIFIERS:
                    if(buffIndx < 4) {
                        frame->msgIdentifiers[buffIndx-2] = incomingBuff;
                        buffIndx++;
                    } 
                    
                    if(buffIndx == 4) {
                        currState = GET_LENGTH;
                    }
                    break;
                case GET_LENGTH:
                    if(buffIndx < 6) {
                        tempBuf[buffIndx-4] = incomingBuff;
                        buffIndx++;   
                    } 

                    if(buffIndx == 6) {
                        std::memcpy(&frame->length, tempBuf, 2); // Copy length into frame struct
                        frame->payload = new uint8_t[frame->length]; // Allocate space for payload
                        currState = GET_DATA;
                        // Cleanup tempBuf
                        std::memset(tempBuf, 0, 2);
                    }
                    break;
                case GET_DATA:
                    if(buffIndx < 6 + frame->length) { // If you haven't read all of the payload data yet
                        frame->payload[buffIndx - 6] = incomingBuff;
                        buffIndx++;
                    }

                    if(buffIndx == 6 + frame->length) {
                        currState = GET_CHECKSUM_A;
                    }
                    break;
                case GET_CHECKSUM_A:
                    frame->ckA = incomingBuff;
                    buffIndx++;
                    currState = GET_CHECKSUM_B;
                    break;
                case GET_CHECKSUM_B:
                    frame->ckB = incomingBuff;

                    // Calculate and check checksums
                    bool validChecksums = isChecksumsValid(frame);

                    // Check checksums
                    if(validChecksums) {
                        currState = PROCESS_FRAME;
                    } else {
                        buffIndx = 0;
                        resetFrame(frame);
                        currState = WAIT_PREAMBLE_1;
                    }
                    break;
            }
            lastReadTime = millis();
        } else {
            vTaskDelay(1); // Poll every millisecond
        }
    }

    // Cleanup tempBuff
    delete[] tempBuf;
    return frame;
}

ubx_ack_msg getUbxAck(ubx_msg_class msgClass, ubx_msg_id msgId) {
    // Wait for an ack or nak frame for 1 second max
    bool continueCheck = true;
    bool ackReceived = false;

    unsigned long startTime = millis();
    do {
        ubxFrame_t * frame = getFrameFromExternalGps(ACK_MSG_TIMEOUT);        

        if(frame->isTimeout) {
            Serial.println("No data/valid data in buffer, timed out. Attempting check again...");
        } else if (frame->msgIdentifiers[0] == UBX_ACK_NAK_MSG_CLASS && frame->msgIdentifiers[1] == UBX_ACK_NAK_MSG_ID) {
            if(frame->length != 2) {
                Serial.println("Invalid payload length for NAK msg. Discarding frame and attempting check again..."); 
            } else if(frame->payload[0] == msgClass && frame->payload[1] == msgId) {
                Serial.println("NAK message received"); 
                ackReceived = false;
                continueCheck = false;
            }
        } else if (frame->msgIdentifiers[0] == UBX_ACK_ACK_MSG_CLASS && frame->msgIdentifiers[1] == UBX_ACK_ACK_MSG_ID) {
            if(frame->length != 2) {
                Serial.println("Invalid payload length for ACK msg. Discarding frame and attempting check again..."); 
            } else if(frame->payload[0] == msgClass && frame->payload[1] == msgId) {
                Serial.println("ACK message received"); 
                ackReceived = true;
                continueCheck = false;
            }
        } else {
            Serial.println("Message that is neither an ACK or NAK received. Discarding frame and attempting check again..."); 
        }

        delete frame;
    } while(millis() - startTime < ACK_MSG_TIMEOUT && continueCheck); // Max time ACK/NAK takes is 1 second

    if(!continueCheck) {
        if(ackReceived) return UBX_ACK_RECEIVED;
        else return UBX_NAK_RECEIVED;
    } else {
        Serial.println("Function timed out. No ACK/NAK received");
        return UBX_ACK_TIMEOUT;
    }
}

gps_data_t processNavPvtFrame(ubxFrame_t * frame) { 
    gps_data_t data;
    data.isNatPvtMsg = true;
    data.isInvalid = false;

    // Check if frame is not a NAV-PVT-MSG frame
    if(frame->msgIdentifiers[0] != UBX_NAV_PVT_MSG_CLASS || frame->msgIdentifiers[1] != UBX_NAV_PVT_MSG_ID) {
        data.isNatPvtMsg = false;
        return data;
    }

    // Check if length is incorrect for a NAV-PVT-MSG frame
    if(frame->length != 92) {
        Serial.print("Invalid length: ");
        Serial.println(frame->length);
        data.isInvalid = true;
        return data;
    }
    
    // Check if time and date received are invalid
    data.isValidTime = (frame->payload[11] & 0x03) == 0x03; // Get 0th and 1st bit of 11th byte (validDate and validTime)
    data.gnssFixOK = (frame->payload[21] & 0x01) == 0x01; // Get 0th bit of 21st byte (gnssFixOK)
    if(!data.isValidTime || !data.gnssFixOK) {
        if(!data.isValidTime) {
            Serial.print("Invalid time");
            Serial.println(frame->payload[11] & 0x03);
        } else{
            Serial.println("Invalid GNSS fix");
        }
        data.isInvalid = true;
        return data;
    }

    // Get date
    data.year = frame->payload[4];
    data.month = frame->payload[6];
    data.day = frame->payload[7];

    // Get time
    data.hour = frame->payload[8];
    data.min = frame->payload[9];
    data.second = frame->payload[10];
    data.nano = frame->payload[16];

    // Get position
    std::memcpy(&data.lon, &frame->payload[24], 4);
    std::memcpy(&data.lat, &frame->payload[28], 4);

    // Get pDOP
    std::memcpy(&data.pdop, &frame->payload[76], 2);

    return data;
}

void gpsExternalTask(void *parameters) {
    uint32_t command = STOP_TRACKING;
    QueueHandle_t gpsToLTEQueue = ((gpsExternalDataQueues_t *)parameters)->gpsToLTEQueue;
    QueueHandle_t gpsToSDCardQueue = ((gpsExternalDataQueues_t *)parameters)->gpsToSDCardQueue;
    
    while(1) {
        switch(command) {
            case STOP_TRACKING:
                // Wait for RUN_TRACKING command from main
                if(xTaskNotifyWait(0x00, ULONG_MAX, &command, portMAX_DELAY) && command == RUN_TRACKING) {
                    Serial.println("Run tracking command received!");
                    startGpsExternal();
                    bool configSuccess = configGpsExternal();
                    if(!configSuccess) {
                        Serial.println("Could not configure GPS sensor. Returning to STOP_TRACKING mode");
                        command = STOP_TRACKING;
                    } else {
                        Serial.println("Successfully started RUN_TRACKING");
                    }
                }
                break;
            case RUN_TRACKING:
                // Get GPS frame from UART stream
                ubxFrame_t * frame = getFrameFromExternalGps(GPS_MSG_TIMEOUT);

                // Check if frame was successfully read
                if(frame->isTimeout) {
                    Serial.println("Frame read timed out! Can't process frame.");
                } else {
                    // If frame was successfully read, process it
                    gps_data_t * data = new gps_data_t;
                    *data = processNavPvtFrame(frame);

                    // If data is valid, send to LTE and SD card queues
                    if(!data->isNatPvtMsg) {
                        Serial.println("Not a NAV-PVT-MSG frame, ignored frame.");
                    } else if (data->isInvalid) {
                        Serial.println("Invalid data! Did not add to queue.");
                    } else {
                        // Add item to LTE queue if there is space
                        // If there isn't space, delete oldest value, add new value
                        if(xQueueSend(gpsToLTEQueue, data, 0) != pdPASS) {
                            gps_data_t tempData; 
                            xQueueReceive(gpsToLTEQueue, &tempData, 0);   
                            if(xQueueSend(gpsToLTEQueue, data, 0) == pdPASS) {
                                //Serial.println("Queue full! Removed oldest value and sent frame to LTE Queue!");
                            } else {
                                Serial.println("Queue full! Removed oldest value but couldn't send frame to LTE Queue.");
                            }
                        } else {
                            //Serial.println("Sent frame to LTE Queue!");
                        }

                        // Add item to SD card queue if there is space
                        // If there isn't space, delete oldest value, add new value
                        if(xQueueSend(gpsToSDCardQueue, data, 0) != pdPASS) {
                            gps_data_t tempData;
                            xQueueReceive(gpsToSDCardQueue, &tempData, 0);   
                            if(xQueueSend(gpsToSDCardQueue, data, 0) == pdPASS) {
                                //Serial.println("Queue full! Removed oldest value and sent frame to SD card Queue!");
                            } else {
                                Serial.println("Queue full! Removed oldest value but couldn't send frame to SD card Queue.");
                            }
                        } else {
                            //Serial.println("Sent frame to SD card Queue!");
                        }
                    }

                    // Cleanup data pointer
                    delete data;
                }

                // Check if main sent STOP_TRACKING command
                if(xTaskNotifyWait(0x00, ULONG_MAX, &command,0) && command == STOP_TRACKING) {
                    stopGpsExternal();
                    Serial.println("Stop tracking command received!");
                }

                // Cleanup frame pointer
                delete frame;
                
                break;
        }
    }
}