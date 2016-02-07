/**
 * ----------------------------------------------------------------------------
* Typical pin layout used - MFRC522:
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Reader/PCD   Uno           Mega      Nano v3    Leonardo/Micro   Pro Micro
 * Signal      Pin          Pin           Pin       Pin        Pin              Pin
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             5         D9         RESET/ICSP-5     RST
 * SPI SS 1    SDA(SS)      10            53        D10        5                10
 * SPI SS 2    SDA(SS)      7             ?         D7         3                ?
 * SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           16
 * SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           14
 * SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           15
 * 
 * Typical pin layout used - Loconet: 
 * RX = ICP pin (see arduino.cc for details on each board). I'm using the Leonardo board => Pin4
 * TX can be freely choosed, but the LN_TX_PIN define (row 48) should be updated. 
 * -----------------------------------------------------------------------------------------
 *             Loconet      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Signal       Uno           Mega      Nano v3    Leonardo/Micro   Pro Micro
 * -----------------------------------------------------------------------------------------
 *             RX           ICP           ICP       ICP (8)   ICP (4)          ICP      
 *             TX                                        6     6                
 *
 */

#ifndef RFID2LN_H_
#define RFID2LN_H_

#include <LocoNet.h>

//#define UNO_LM /*my special UNO connections, to can use the same adaptor as for leonardo*/

#if ARDUINO >= 10500 //the board naming scheme is supported from Arduino 1.5.0
 #if (defined(ARDUINO_AVR_UNO) && !defined(UNO_LM)) || defined(ARDUINO_AVR_NANO)
  #define LN_TX_PIN       6           /* Arduino Pin used as Loconet Tx; Rx Pin is always the ICP Pin */
  #define RST_PIN         9           /* Configurable, see typical pin layout above*/
  #define SS_1_PIN       10           /* Configurable, see typical pin layout above*/   
  #define SS_2_PIN        7           /* Configurable, see typical pin layout above*/   
#elif defined(ARDUINO_AVR_LEONARDO) || defined(UNO_LM) 
  #define LN_TX_PIN       6           /* Arduino Pin used as Loconet Tx; Rx Pin is always the ICP Pin */
  #define RST_PIN         9           /* Configurable, see typical pin layout above*/
  #define SS_1_PIN        5           /* Configurable, see typical pin layout above*/   
  #define SS_2_PIN        3           /* Configurable, see typical pin layout above*/   
  #endif     
#else //older arduino IDE => initialising each board as it is used. I'm using Leonardo
  #define LN_TX_PIN       6           /* Arduino Pin used as Loconet Tx; Rx Pin is always the ICP Pin */
  #define RST_PIN         9           /* Configurable, see typical pin layout above*/
  #define SS_1_PIN        5           /* Configurable, see typical pin layout above*/   
  #define SS_2_PIN        3           /* Configurable, see typical pin layout above*/   
#endif 

#define NR_OF_RFID_PORTS     2
#define TOTAL_NR_OF_PORTS    8

#define MANUF_ID        13          /* DIY DCC*/
#define BOARD_TYPE      5           /* something for sv.init*/

    // --------------------------------------------------------
    // OPC_PEER_XFER SV_CMD's
    // --------------------------------------------------------

#define SV_CMD_WRITE            0x01    /* Write 1 byte of data from D1*/
#define SV_CMD_READ             0x02    /* Initiate read 1 byte of data into D1*/
#define SV_CMD_MASKED_WRITE     0x03    /* Write 1 byte of masked data from D1. D2 contains the mask to be used.*/
#define SV_CMD_WRITE4           0x05    /* Write 4 bytes of data from D1..D4*/
#define SV_CMD_READ4            0x06    /* Initiate read 4 bytes of data into D1..D4*/
#define SV_CMD_DISCOVER         0x07    /* Causes all devices to identify themselves by their MANUFACTURER_ID, DEVELOPER_ID, PRODUCT_ID and Serial Number*/
#define SV_CMD_IDENTIFY         0x08    /* Causes an individual device to identify itself by its MANUFACTURER_ID, DEVELOPER_ID, PRODUCT_ID and Serial Number*/
#define SV_CMD_CHANGE_ADDR      0x09    /* Changes the device address to the values specified in <DST_L> + <DST_H> in the device that matches */
                                        /* the values specified in <SV_ADRL> + <SV_ADRH> + <D1>..<D4> that we in the reply to the Discover or Identify command issued previously*/
#define SV_CMD_RECONFIGURE      0x4F    /* Initiates a device reconfiguration or reset so that any new device configuration becomes active*/

    // Replies
#define SV_CMDR_WRITE           0x41    /* Transfers a write response in D1*/
#define SV_CMDR_READ            0x42    /* Transfers a read response in D1*/
#define SV_CMDR_MASKED_WRITE    0x43    /* Transfers a masked write response in D1*/
#define SV_CMDR_WRITE4          0x45    /* Transfers a write response in D1..D4*/
#define SV_CMDR_READ4           0x46    /* Transfers a read response in D1..D4*/
#define SV_CMDR_DISCOVER        0x47    /* Transfers an Discover response containing the MANUFACTURER_ID, DEVELOPER_ID, PRODUCT_ID and Serial Number*/
#define SV_CMDR_IDENTIFY        0x48    /* Transfers an Identify response containing the MANUFACTURER_ID, DEVELOPER_ID, PRODUCT_ID and Serial Number*/
#define SV_CMDR_CHANGE_ADDR     0x49    /* Transfers a Change Address response.*/
#define SV_CMDR_RECONFIGURE     0x4F    /* Acknowledgement immediately prior to a device reconfiguration or reset*/

//Other LN definitions
#define SEN_QUERY_LOW_ADDRESS   0x79    /* 1017 & 0x007F - 7 bits low address for the sensors query address 1017*/
#define SEN_QUERY_HIGH_ADDRESS  0x07    /* (1017 >> 8) & 0x07 - high address bits for the sensors query address 1017*/
#define LN_MESS_LEN_PEER 16

//Version
#define VER_LOW         0x01
#define VER_HIGH        0X00

#define UID_LEN         7
#define LN_BUFF_LEN    10

#define MAX_EMPTY_READS 2


extern void dump_byte_array(byte *buffer, byte bufferSize);
extern bool compareUid(byte *buffer1, byte *buffer2, byte bufferSize);
extern void copyUid(byte *buffIn, byte *buffOut, byte bufferSize);
extern void setMessageHeader(uint8_t port, uint8_t index);
extern uint8_t processXferMess(lnMsg *LnRecMsg, lnMsg *LnSendMsg);
extern uint8_t lnCalcCheckSumm(uint8_t *cMessage, uint8_t cMesLen);
extern void boardSetup(void);
extern void calcSenAddr(uint8_t);
extern void printSensorData(uint8_t);
extern void lnDecodeMessage(lnMsg *LnPacket);
extern void activateRec(MFRC522 mfrc522);

extern uint8_t boardVer[];
extern char verLen;

extern LocoNetSystemVariableClass sv;
extern lnMsg       *LnPacket;
extern lnMsg       SendPacket ;
extern lnMsg       SendPacketSensor[] ;
extern SV_STATUS   svStatus;
extern boolean     deferredProcessingNeeded;

extern uint8_t ucBoardAddrHi;  //board address high; always 1
extern uint8_t ucBoardAddrLo;  //board address low; default 88

extern uint8_t ucAddrHiSen[NR_OF_RFID_PORTS];    //sensor address high
extern uint8_t ucAddrLoSen[NR_OF_RFID_PORTS];    //sensor address low
extern uint8_t ucSenType[NR_OF_RFID_PORTS]; //input
extern uint16_t uiAddrSenFull[NR_OF_RFID_PORTS];

extern uint8_t uiLnSendCheckSumIdx;
extern uint8_t uiLnSendLength; //14 bytes
extern uint8_t uiLnSendMsbIdx;
extern uint8_t uiStartChkSen;

extern uint8_t oldUid[NR_OF_RFID_PORTS][UID_LEN];

extern boolean bSerialOk;

typedef struct {
  uint16_t addr;
  uint8_t  state;
  uint8_t  idx;
} __outType;

extern __outType outputs[];
extern uint8_t outsNr;
extern boolean bUpdateOutputs;

#endif //ifndef RFID2LN_H_

