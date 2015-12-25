/**
 * ----------------------------------------------------------------------------
 * This is a combination between the MFRC522 library (https://github.com/miguelbalboa/rfid )
 * and the arduino Loconet library
 *
 * In this sketch, the UID of the MFRC522 compatible cards/tags is read and send over
 * loconet bus.
 * In the same time, the communication on the Loconet bus is decoded, allowing the
 * reprogramming of the board (new address).
 *
 * Typical pin layout used - MFRC522:
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Reader/PCD   Uno           Mega      Nano v3    Leonardo/Micro   Pro Micro
 * Signal      Pin          Pin           Pin       Pin        Pin              Pin
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             5         D9         RESET/ICSP-5     RST
 * SPI SS      SDA(SS)      10            53        D10        5                10
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

#include <SPI.h>
#include <MFRC522.h>
#include <LocoNet.h>
#include "rfid2ln.h"
#include <EEPROM.h>

//#define _SER_DEBUG
#define EE_ERASE   0

MFRC522 mfrc522[NR_OF_PORTS];
uint8_t boardVer[] = "RFID2LN V1";
char verLen = sizeof(boardVer);

LocoNetSystemVariableClass sv;
lnMsg       *LnPacket;
lnMsg       SendPacket ;
lnMsg       SendPacketSensor[LN_BUFF_LEN] ;

SV_STATUS   svStatus = SV_OK;
boolean     deferredProcessingNeeded = false;

uint8_t ucBoardAddrHi = 1;  //board address high; always 1
uint8_t ucBoardAddrLo = 88;  //board address low; default 88

uint8_t ucAddrHiSen[NR_OF_PORTS];    //sensor address high
uint8_t ucAddrLoSen[NR_OF_PORTS];    //sensor address low
uint8_t ucSenType[NR_OF_PORTS] = {0x0F}; //input
uint16_t uiAddrSenFull[NR_OF_PORTS];

uint8_t uiLnSendCheckSumIdx = 13;
uint8_t uiLnSendLength = 14; //14 bytes
uint8_t uiLnSendMsbIdx = 12;
uint8_t uiStartChkSen;

uint8_t oldUid[NR_OF_PORTS][UID_LEN] = {0};

boolean bSerialOk = false;

byte mfrc522Cs[] = {SS_1_PIN, SS_2_PIN};

uint8_t uiBufWrIdx = 0;
uint8_t uiBufRdIdx = 0;
uint8_t uiBufCnt = 0;

/**
 * Initialize.
 */
void setup() {
  uint32_t uiStartTimer;
  uint16_t uiElapsedDelay;
  uint16_t uiSerialOKDelay = 5000;

  Serial.begin(115200); // Initialize serial communications with the PC
  uiStartTimer = millis();
  do { //wait for the serial interface, but maximal 1 second.
    uiElapsedDelay = millis() - uiStartTimer;
  } while ((!Serial) && (uiElapsedDelay < uiSerialOKDelay));    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)

  if (Serial) { //serial interface ok
    bSerialOk = true;
    Serial.println(F("************************************************"));
  }

  //initialize the LocoNet interface
  LocoNet.init(LN_TX_PIN); //Always use the explicit naming of the Tx Pin to avoid confusions
  sv.init(MANUF_ID, BOARD_TYPE, 1, 1); //to see if needed just once (saved in EEPROM)

#if EE_ERASE
  for(uint8_t i = 0; i<verLen; i++){
    EEPROM.write(255 - verLen + i, 0xff);
  }
#endif

  boardSetup();
  for (uint8_t i = 0; i < NR_OF_PORTS; i++) {
     calcSenAddr(i);
  }

  SPI.begin();        // Init SPI bus
//  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0)); //spi speed of MFRC522 - 10 MHz
  
  for (uint8_t i = 0; i < NR_OF_PORTS; i++) {
    mfrc522[i].PCD_Init(mfrc522Cs[i], RST_PIN);
    if (bSerialOk) {
      printSensorData(i);
    }
  }

  if (bSerialOk) {
    Serial.println(F("************************************************"));
  }
}

/**
 * Main loop.
 */
void loop() {
  SV_STATUS svStatus;
  unsigned long uiStartTime;
  unsigned long uiActTime;
  unsigned char i = 0;
  unsigned char j = 0;
  uint16_t uiDelayTime = 500; //*ms

/*************
 * Read the TAGs
 */
  for (uint8_t port = 0; port < NR_OF_PORTS; port++) {
    if(uiBufCnt < LN_BUFF_LEN){  //if buffer not full
      if ( mfrc522[port].PICC_IsNewCardPresent() && mfrc522[port].PICC_ReadCardSerial()) { //if tag data
        static bool delaying = false;
        if (!delaying){   //Avoid to many/to fast reads of the same tag
          // Show some details of the PICC (that is: the tag/card)
          if (bSerialOk) {
            Serial.print(F("Port: "));
            Serial.print(port);
            Serial.print(F(" Card UID:"));
            dump_byte_array(mfrc522[port].uid.uidByte, mfrc522[port].uid.size);
            Serial.println();
          }

          uiStartTime = millis();
          delaying = true;

          setMessageHeader(port, uiBufWrIdx); //if the sensor address was changed, update the header
 
          /****
          * Put the new data in buffer
          */
          SendPacketSensor[uiBufWrIdx].data[uiLnSendCheckSumIdx] = uiStartChkSen; //start with header check summ
          SendPacketSensor[uiBufWrIdx].data[uiLnSendMsbIdx] = 0; //clear the byte for the ms bits
          for (i = 0, j = 5; i < UID_LEN; i++, j++) {
            if (mfrc522[port].uid.size > i) {
              SendPacketSensor[uiBufWrIdx].data[j] = mfrc522[port].uid.uidByte[i] & 0x7F; //loconet bytes haver only 7 bits;
              // MSbit is transmited in the SendPacket.data[10]
              if (mfrc522[port].uid.uidByte[i] & 0x80) {
                 SendPacketSensor[uiBufWrIdx].data[uiLnSendMsbIdx] |= 1 << i;
              }
              SendPacketSensor[uiBufWrIdx].data[uiLnSendCheckSumIdx] ^= SendPacketSensor[uiBufWrIdx].data[j]; //calculate the checksumm
            } else { //if (mfrc522[port].uid.
              SendPacketSensor[uiBufWrIdx].data[j] = 0;
            }
          } //for(i=0

          SendPacketSensor[uiBufWrIdx].data[uiLnSendCheckSumIdx] ^= SendPacketSensor[uiBufWrIdx].data[uiLnSendMsbIdx]; //calculate the checksumm

#ifdef _SER_DEBUG
          if (bSerialOk) {
            Serial.print(F("LN mess to be sent:"));
            dump_byte_array(SendPacketSensor[uiBufWrIdx].data, uiLnSendLength);
            Serial.println();
          }
#endif
          if(uiBufWrIdx < LN_BUFF_LEN){
            uiBufWrIdx++;
          } else {
            uiBufWrIdx = 0;
          }
          uiBufCnt++;
 
          copyUid(mfrc522[port].uid.uidByte, oldUid[port], mfrc522[port].uid.size);
        } else { //if(!delaying)
          uiActTime = millis();
          if (compareUid(	mfrc522[port].uid.uidByte, oldUid[port], mfrc522[port].uid.size)) { //same UID
            if ((uiActTime - uiStartTime) > uiDelayTime) {
              delaying = false;
            } //if((uiActTime
          } else { //new UID
            delaying = false;
          }
        } //else

        // Halt PICC
        mfrc522[port].PICC_HaltA();
        // Stop encryption on PCD
        mfrc522[port].PCD_StopCrypto1();

      } //if ( mfrc522_1.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
    }   // if(uiBufCnt < LN_BUFF_LEN){
  } //for(uint8_t i = 0; i<NR_OF_PORTS; i++)

  /******
   * send the tag data in the loconet bus
   */
  if(uiBufCnt > 0){
#ifdef _SER_DEBUG
        if (bSerialOk) {
          Serial.print(F("LN send mess:"));
          dump_byte_array(SendPacketSensor[uiBufRdIdx].data, uiLnSendLength);
          Serial.println();
        }
#endif

     LN_STATUS lnSent = LocoNet.send( &SendPacketSensor[uiBufRdIdx], LN_BACKOFF_MAX - (ucBoardAddrLo % 10));   //trying to differentiate the ln answer time

     if(lnSent = LN_DONE){ //message sent OK
        if(uiBufRdIdx < LN_BUFF_LEN){
           uiBufRdIdx++;
        } else {
           uiBufRdIdx=0;
        }
        uiBufCnt--;
     } //if(lnSent = LN_DONE)
  } //if(uiBufCnt > 0){

  /************
   * Addresses programming over loconet
   */
   
  LnPacket = LocoNet.receive() ;
  if ( LnPacket && ((LnPacket->data[2] != ucBoardAddrLo) || (LnPacket->data[4] != ucBoardAddrHi))) { //new message sent by other
    uint8_t msgLen = getLnMsgSize(LnPacket);

    //Change the board & sensor addresses. Changing the board address is working
    if (msgLen == 0x10) { //XFERmessage, check if it is for me. Used to change the address
      //svStatus = sv.processMessage(LnPacket);

      processXferMess(LnPacket, &SendPacket);

      /*blocking. If it works, to find out a non blocking version*/
      LN_STATUS lnSent;
      do{
         LocoNet.send( &SendPacket, LN_BACKOFF_MAX - (ucBoardAddrLo % 10));   //trying to differentiate the ln answer time
      } while(lnSent != LN_DONE);
      
      // Rocrail compatible addressing
      for (uint8_t i = 0; i < NR_OF_PORTS; i++) {
        calcSenAddr(i);

#ifdef _SER_DEBUG
        if (bSerialOk) {
          printSensorData(i);
        }
#endif
      }
    } //if(msgLen == 0x10)
  }
}



