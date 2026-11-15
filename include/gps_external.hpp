#pragma once
#include <Arduino.h>

#define RXD2 4 // Define RX pin
#define TXD2 5 // Define TX pin

#define GPS_BAUD 115200 // Default baud rate for sensor

#define READ_TIMEOUT 500 // 0.5 seconds
#define GPS_MSG_TIMEOUT 2000 // 2 seconds
#define ACK_MSG_TIMEOUT 1000 // 1 second

#define UBX_PREA_1 0xb5 // UBX frame preamble 1
#define UBX_PREA_2 0x62 // UBX frame preamble 2

enum ubx_ack_msg {
    UBX_ACK_RECEIVED,
    UBX_NAK_RECEIVED,
    UBX_ACK_TIMEOUT
};

enum ubx_msg_class {
    UBX_NAV_PVT_MSG_CLASS = 0x01,
    UBX_CFG_VALSET_MSG_CLASS = 0x06,
    UBX_ACK_ACK_MSG_CLASS = 0x05,
    UBX_ACK_NAK_MSG_CLASS = 0x05
};

enum ubx_msg_id {
    UBX_NAV_PVT_MSG_ID = 0x07,
    UBX_CFG_VALSET_MSG_ID = 0x8a,
    UBX_ACK_ACK_MSG_ID = 0x01,
    UBX_ACK_NAK_MSG_ID = 0x00
};

enum ubx_config_data_key {
    CFG_UART1OUTPROT_UBX_KEY = 0x10740001,
    CFG_UART1OUTPROT_NMEA_KEY = 0x10740002,
    CFG_MSGOUT_UBX_NAV_PVT_UART1_KEY = 0x20910007
};

enum ubx_config_data_value {
    CFG_UART1OUTPROT_UBX_ENA = 1,
    CFG_UART1OUTPROT_NMEA_DISABLE = 0,
    CFG_MSGOUT_UBX_NAV_PVT_UART1_ENA = 1
};

typedef struct gpsExternalDataParams {
  // Queues for outputing GPS data
  QueueHandle_t gpsToLTEQueue = NULL;
  QueueHandle_t gpsToSDCardQueue = NULL;
} gpsExternalDataQueues_t;

enum gpsExternalCommand {
    RUN_TRACKING = 0,
    STOP_TRACKING = 1
};

typedef struct gps_data {
    int32_t lon;
    int32_t lat;
    uint8_t gnssFixOK; // Is position valid

    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t second;
    int32_t nano; // Fraction of second, range -1e9 .. 1e9 (UTC)
    uint8_t isValidTime; // Is time valid

    uint16_t pdop;

    bool isInvalid;
    bool isNatPvtMsg;
} gps_data_t;

enum uartState {
    WAIT_PREAMBLE_1,
    WAIT_PREAMBLE_2,
    GET_MESSAGE_IDENTIFIERS, // Wait for message class and ID
    GET_LENGTH,
    GET_DATA,
    GET_CHECKSUM_A,
    GET_CHECKSUM_B,
    PROCESS_FRAME
};

typedef struct ubxFrame {
    uint8_t prea1;
    uint8_t prea2;
    uint8_t msgIdentifiers[2]; // 0 = Message class, 1 = Message ID
    uint16_t length; // Payload length
    uint8_t * payload; // <length> bytes long
    uint8_t ckA; // Checksum A
    uint8_t ckB; // Checksum B

    bool isTimeout; // Stores whether the "get GPS frame" function timed out before processing the full frame

    ~ubxFrame() {
        delete[] payload;
    }
} ubxFrame_t;

/**
 * @brief Creates a frame that can be sent to configure the external sensor's UBX mode
 *
 * @param key Key of config frame
 * @param data Array of data of config frame
 * @param size Size of data array
 * @return UBX frame for the config key and data provided
 */
ubxFrame_t * getUbxConfigFrame(ubx_config_data_key key, uint8_t * data, uint16_t size);

/**
 * @brief Send frame to external sensor
 *
 * @param frame Pointer to frame to send
 */
void sendMsgToGPS(ubxFrame_t * frame);

/**
 * @brief Attempt to configure external sesnor to use UBX protocol
 *
 * @return Whether config attempt was successful
 */
bool configGpsExternal();

/**
 * @brief Open UART communication with external sensor 
 */
void startGpsExternal();

/**
 * @brief Close UART communication with external sensor 
 */
void stopGpsExternal();

/**
 * @brief Reset frame values (set values to zero and cleanup payload)
 * 
 * @param Frame to reset
 */
void resetFrame(ubxFrame_t * frame);

/**
 * @brief Checks if the two checksums of a frame are valid
 * 
 * @param Frame to check
 * @return Whether both checksums are valid
 */
bool isChecksumsValid(ubxFrame_t * frame);

/**
 * @brief Calculates the two checksums of a frame
 * 
 * @param Frame calculate checksums of
 * @return Array of 2 checksums
 */
uint8_t * calcChecksums(ubxFrame_t * frame);

/**
 * @brief Attempts to get valid frame from external sensor via UART
 * 
 * @param maxTotalTime Max time the function can take to get a frame with valid checksums
 * @return Frame received. If no frame was received, the returned frame's "isTimeout" bool is set to true
 */
ubxFrame_t * getFrameFromExternalGps(int maxTotalTime);

/**
 * @brief Check for ACK/NAK response of a message sent to the external sensor
 * 
 * @param msgClass message class of message sent to sensor
 * @param msgID message ID of message sent to sensor
 * @return Whether a response to the message sent was received, and the type of response:
 * - "UBX_ACK_RECEIVED" if ACK received
 * - "UBX_NAK_RECEIVED" if NAK received
 * - "UBX_ACK_TIMEOUT" if no ACK/NAK received within an "ACK_MSG_TIMEOUT" amount of time
 */
ubx_ack_msg getUbxAck(ubx_msg_class msgClass, ubx_msg_id msgId);

/**
 * @brief Check if frame is a valid NAV-PVT frame. If it is, extract GPS data
 * 
 * @param frame Frame to process
 * @return One of the following:
 * - GPS data if frame was a valid NAV-PVT frame
 * - Whether frame was invalid/not a NAV-PVT frame via data's "isInvalid" and "isNatPvtMsg" bools
 */
gps_data_t processNavPvtFrame(ubxFrame_t * frame);

/**
 * @brief External sensor FreeRTOS task. Performs the following operations:
 * - Starts/stops tracking GPS data based on command received  
 * - Gets GPS frames from UART stream
 * - Processes GPS data 
 * - Sends a copy of the GPS data as a "gps_data_t" to LTE and SD card Queues
 * 
 * @param parameters The "GPS to LTE" and "GPS to SD card" queues
 */
void gpsExternalTask(void *parameters);