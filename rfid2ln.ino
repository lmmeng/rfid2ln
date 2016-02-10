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
 * SPI SS 1    SDA(SS)      10            53        D10        5                ?
 * SPI SS 2    SDA(SS)      7             53        D7         3                ?
 * SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           ?
 * SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           ?
 * SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           ?
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

MFRC522 mfrc522[NR_OF_RFID_PORTS];
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

uint8_t ucAddrHiSen[NR_OF_RFID_PORTS];    //sensor address high
uint8_t ucAddrLoSen[NR_OF_RFID_PORTS];    //sensor address low
uint8_t ucSenType[NR_OF_RFID_PORTS] = {0x0F}; //input
uint16_t uiAddrSenFull[NR_OF_RFID_PORTS];

uint8_t uiLnSendCheckSumIdx = 13;
uint8_t uiLnSendLength = 14; //14 bytes
uint8_t uiLnSendMsbIdx = 12;
uint8_t uiStartChkSen;

uint8_t oldUid[NR_OF_RFID_PORTS][UID_LEN] = {0}; 

boolean bSerialOk = false;

byte mfrc522Cs[] = {SS_1_PIN, SS_2_PIN};

#if USE_INTERRUPT
  byte mfrc522Irq[] = {IRQ_1_PIN, IRQ_2_PIN};
#endif

uint8_t uiBufWrIdx = 0;
uint8_t uiBufRdIdx = 0;
uint8_t uiBufCnt = 0;

__outType outputs[TOTAL_NR_OF_PORTS - NR_OF_RFID_PORTS]; /*maximum number of outputs*/
uint8_t outsNr = 0;
boolean bUpdateOutputs = false;

uint8_t uiRfidPort = 0;

uint8_t uiNrEmptyReads[NR_OF_RFID_PORTS] = {3}; //send LN message if at supplying the tag is on reader.

uint8_t uiActReaders = 0;
uint8_t uiFirstReaderIdx = 0;


#if USE_INTERRUPTS
volatile boolean bNewInt[NR_OF_RFID_PORTS] = {false};
unsigned char regVal = 0x7F;
void activateRec(MFRC522 mfrc522);
void clearInt(MFRC522 mfrc522);

readCardIntArray readCardInt[] = {readCard1, readCard2};
#endif

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
  for (uint8_t i = 0; i < verLen; i++) {
    EEPROM.write(255 - verLen + i, 0xff);
  }
#endif

  boardSetup();

  SPI.begin();        // Init SPI bus
  //  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0)); //spi speed of MFRC522 - 10 MHz

#if USE_INTERRUPT
  regVal = 0xA0; //rx irq
#endif
  
  for (uint8_t i = 0; i < NR_OF_RFID_PORTS; i++) {
    if (bSerialOk) {
      Serial.print(F("Before init; reader = "));
      Serial.println(i+1);
    }
    mfrc522[i].PCD_Init(mfrc522Cs[i], RST_PIN);
    
    /* detect the active readers. If version read != 0 => reader active*/
    if (bSerialOk) {
      Serial.println(F("Before version read "));
    }
    byte readReg = mfrc522[i].PCD_ReadRegister(mfrc522[i].VersionReg);
    if (bSerialOk) {
      Serial.print(F("After version read; version = "));
      Serial.println(readReg, HEX);
    }
    if(readReg){

      if(0 == uiActReaders){ //save the number of the first active reader
         uiFirstReaderIdx = i;
         uiRfidPort = uiFirstReaderIdx; //initialize the starting reader counter
      }
      uiActReaders++;
      calcSenAddr(i);


#if USE_INTERRUPT
      pinMode(mfrc522Irq[i], INPUT_PULLUP);

      /* 
       *  Allow the ... irq to be propagated to the IRQ pin
       *  For test purposes propagate the IdleIrq and loAlert
       */
      mfrc522[i].PCD_WriteRegister(mfrc522[i].ComIEnReg,regVal);

      delay(10);
      attachInterrupt(digitalPinToInterrupt(mfrc522Irq[i]), readCard[i](), FALLING);
      bNewInt[i] = false;
#endif

      if (bSerialOk) {
        printSensorData(i);
      }
    } //if(readReg)
  } //for(uint8_t i = 0
  
  if (bSerialOk) {
    Serial.print(F("Nr. of active RFID readers: "));
    Serial.println(uiActReaders);

    Serial.println(F("************************************************"));
  }
}



/**
 * Main loop.
 */
void loop() {
  /*************
   * Read the TAGs
   */
  //  for (uint8_t port = 0; port < NR_OF_RFID_PORTS; port++) {
  if (uiActReaders > 0) {
    if (uiBufCnt < LN_BUFF_LEN) { //if buffer not full
#if USE_INTERRUPT
      if(bNewInt[uiRfidPort]){
        bNewInt[uiRfidPort] = false;
#else
      if(mfrc522[uiRfidPort].PICC_IsNewCardPresent()) {
#endif
        if (mfrc522[uiRfidPort].PICC_ReadCardSerial()) { //if tag data
          if (uiNrEmptyReads[uiRfidPort] > 2) { //send an uid only once
            // Show some details of the PICC (that is: the tag/card)
            if (bSerialOk) {
              Serial.print(F("Port: "));
              Serial.print(uiRfidPort);
              Serial.print(F(" Card UID:"));
              dump_byte_array(mfrc522[uiRfidPort].uid.uidByte, mfrc522[uiRfidPort].uid.size);
              Serial.println();
            }

            setMessageHeader(uiRfidPort, uiBufWrIdx); //if the sensor address was changed, update the header

            /****
            * Put the new data in buffer
            */
            SendPacketSensor[uiBufWrIdx].data[uiLnSendCheckSumIdx] = uiStartChkSen; //start with header check summ
            SendPacketSensor[uiBufWrIdx].data[uiLnSendMsbIdx] = 0; //clear the byte for the ms bits
            for (uint8_t i = 0, j = 5; i < UID_LEN; i++, j++) {
              if (mfrc522[uiRfidPort].uid.size > i) {
                SendPacketSensor[uiBufWrIdx].data[j] = mfrc522[uiRfidPort].uid.uidByte[i] & 0x7F; //loconet bytes have only 7 bits;
                // MSbit is transmited in the SendPacket.data[10]
                if (mfrc522[uiRfidPort].uid.uidByte[i] & 0x80) {
                  SendPacketSensor[uiBufWrIdx].data[uiLnSendMsbIdx] |= 1 << i;
                }
                SendPacketSensor[uiBufWrIdx].data[uiLnSendCheckSumIdx] ^= SendPacketSensor[uiBufWrIdx].data[j]; //calculate the checksumm
              } else { //if (mfrc522[port].uid.
                SendPacketSensor[uiBufWrIdx].data[j] = 0;
              }
            } //for(i=0

            SendPacketSensor[uiBufWrIdx].data[uiLnSendCheckSumIdx] ^= SendPacketSensor[uiBufWrIdx].data[uiLnSendMsbIdx]; //calculate the checksumm

            if (uiBufWrIdx < LN_BUFF_LEN) {
              uiBufWrIdx++;
            } else {
              uiBufWrIdx = 0;
            }
            uiBufCnt++;

            copyUid(mfrc522[uiRfidPort].uid.uidByte, oldUid[uiRfidPort], mfrc522[uiRfidPort].uid.size);
          } //if(uiNrEmptyReads[uiRfidPort] > ...){          

          uiNrEmptyReads[uiRfidPort] = 0;

        } //if(mfrc522[uiRfidPort].PICC_ReadCardSerial())

#if USE_INTERRUPT
        clearInt(mfrc522[uiRfidPort]);
        activateRec(mfrc522[uiRfidPort]); //rearm the reading part of the mfrc522
#endif          
      } else { //if newCard / newInt
        /* Reset the sensor indication in Rocrail => RFID can be used as a normal sensor*/
        boolean rc = mfrc522[uiRfidPort].PICC_ReadCardSerial();
        if (!rc) {
          if (/*bSendReset[uiRfidPort] &&*/ (uiNrEmptyReads[uiRfidPort] == MAX_EMPTY_READS)) {
            if (bSerialOk) {
              Serial.println(F("Send reset: "));
            }
            uint16_t uiAddr =  (uiAddrSenFull[uiRfidPort] - 1) / 2;
            SendPacketSensor[uiBufWrIdx].data[0] = 0xB2;
            SendPacketSensor[uiBufWrIdx].data[1] = uiAddr & 0x7F; //ucAddrLoSen;
            SendPacketSensor[uiBufWrIdx].data[2] = ((uiAddr >> 7) & 0x0F) | 0x40; //ucAddrHiSen & 0xEF;
            if ((uiAddrSenFull[uiRfidPort] & 0x01) == 0) {
              SendPacketSensor[uiBufWrIdx].data[2] |= 0x20;
            }
            SendPacketSensor[uiBufWrIdx].data[3] = lnCalcCheckSumm(SendPacketSensor[uiBufWrIdx].data, 3);

            if (uiBufWrIdx < LN_BUFF_LEN) {
              uiBufWrIdx++;
            } else {
              uiBufWrIdx = 0;
            }
            uiBufCnt++;
          
          } //if(bSendReset

          if (uiNrEmptyReads[uiRfidPort] <= (MAX_EMPTY_READS + 1)) {
            uiNrEmptyReads[uiRfidPort]++;
          }
        } //if (!mfrc522.PICC_
      }   // else if ( mfrc522.PICC_IsNewCardPresent()  

      uiRfidPort++;
      if (uiRfidPort == uiActReaders + uiFirstReaderIdx) {
        uiRfidPort = uiFirstReaderIdx;
      }
    } //if(uiBufCnt < LN_BUFF_LEN){
  } //if(NR_OF_RFID_PORTS

  /******
   * send the tag data in the loconet bus
   */
  if (uiBufCnt > 0) {
#ifdef _SER_DEBUG
    if (bSerialOk) {
      Serial.print(F("LN send mess:"));
      dump_byte_array(SendPacketSensor[uiBufRdIdx].data, uiLnSendLength);
      Serial.println();
    }
#endif

    LN_STATUS lnSent = LocoNet.send( &SendPacketSensor[uiBufRdIdx], LN_BACKOFF_MAX - (ucBoardAddrLo % 10));   //trying to differentiate the ln answer time

    if (lnSent = LN_DONE) { //message sent OK
      if (uiBufRdIdx < LN_BUFF_LEN) {
        uiBufRdIdx++;
      } else {
        uiBufRdIdx = 0;
      }
      uiBufCnt--;
    } //if(lnSent = LN_DONE)
  } //if(uiBufCnt > 0){

  /************
   * Addresses programming over loconet
   */

  LnPacket = LocoNet.receive() ;
  if ( LnPacket) { //new message sent by other
    lnDecodeMessage(LnPacket);
  }//if( LnPacket)
}

