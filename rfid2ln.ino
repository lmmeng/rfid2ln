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
 *             TX                                        7     6                
 *
 */

#include <SPI.h>
#include <MFRC522.h>
#include <LocoNet.h>
#include <EEPROM.h>
#include "rfid2ln.h"

//#define _SER_DEBUG
#define EE_ERASE  0

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

MFRC522::MIFARE_Key key;

LocoNetSystemVariableClass sv;
lnMsg       *LnPacket;
lnMsg       SendPacket ;
lnMsg       SendPacketSensor ;
SV_STATUS   svStatus = SV_OK;
boolean     deferredProcessingNeeded = false;

uint8_t ucBoardAddrHi = 1;  //board address high; always 1
uint8_t ucBoardAddrLo = 88;  //board address low; default 88

uint8_t ucAddrHiSen = 0;    //sensor address high
uint8_t ucAddrLoSen = 1;    //sensor address low
uint8_t ucSenType = 0x0F; //input
uint16_t uiAddrSenFull;

uint8_t uiLnSendCheckSumIdx = 13;
uint8_t uiLnSendLength = 14; //14 bytes
uint8_t uiLnSendMsbIdx = 12;
uint8_t uiStartChkSen;

uint8_t oldUid[UID_LEN] = {0};

boolean bSerialOk=false;
  
/**
 * Initialize.
 */
void setup() {
    uint32_t uiStartTimer;
    uint16_t uiElapsedDelay;
    uint16_t uiSerialOKDelay = 5000;
    
    Serial.begin(115200); // Initialize serial communications with the PC
    uiStartTimer = millis();
    do{  //wait for the serial interface, but maximal 1 second.
        uiElapsedDelay = millis() - uiStartTimer;
    } while ((!Serial) && (uiElapsedDelay < uiSerialOKDelay));    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)

    if(Serial) { //serial interface ok
       bSerialOk = true;
       Serial.println(F("************************************************"));
    }
   
    //initialize the LocoNet interface
    LocoNet.init(LN_TX_PIN); //Always use the explicit naming of the Tx Pin to avoid confusions 
    sv.init(MANUF_ID, BOARD_TYPE, 1, 1); //to see if needed just once (saved in EEPROM)

#if EE_ERASE
       for(uint8_t i = 0; i<11; i++){
           EEPROM.write(255 - 11 + i, 0xff);
       }
#endif

    boardSetup();
    calcSenAddr();
    
    SPI.begin();        // Init SPI bus
    mfrc522.PCD_Init(); // Init MFRC522 card

    setMessageHeader();
    uiStartChkSen = SendPacketSensor.data[uiLnSendCheckSumIdx];

    if(bSerialOk){
       printSensorData();
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
  bool delaying;
  unsigned char i=0;
  unsigned char j=0;  
  uint16_t uiDelayTime = 1000;

  if ( mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()){  
     if(!delaying){   //Avoid to many/to fast reads of the same tag
        // Show some details of the PICC (that is: the tag/card)
        if(bSerialOk){
           Serial.print(F("Card UID:"));
           dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
           Serial.println();
        }

#ifdef _SER_DEBUG
        // Show some details of the loconet setup
        printSensorData();
#endif

	      uiStartTime = millis();
	      delaying = true;

        SendPacketSensor.data[uiLnSendCheckSumIdx]= uiStartChkSen; //start with header check summ
        SendPacketSensor.data[uiLnSendMsbIdx]=0; //clear the byte for the ms bits
        for(i=0, j=5; i< UID_LEN; i++, j++){
           if(mfrc522.uid.size > i){
              SendPacketSensor.data[j] = mfrc522.uid.uidByte[i] & 0x7F; //loconet bytes haver only 7 bits;
                                                               // MSbit is transmited in the SendPacket.data[10]
              if(mfrc522.uid.uidByte[i] & 0x80){
                 SendPacketSensor.data[uiLnSendMsbIdx] |= 1 << i;
              }
              SendPacketSensor.data[uiLnSendCheckSumIdx] ^= SendPacketSensor.data[j]; //calculate the checksumm
           } else {
              SendPacketSensor.data[j] = 0;
           }        
        } //for(i=0

        SendPacketSensor.data[uiLnSendCheckSumIdx] ^= SendPacketSensor.data[uiLnSendMsbIdx]; //calculate the checksumm

        if(bSerialOk){
           Serial.print(F("LN send mess:"));
           dump_byte_array(SendPacketSensor.data, uiLnSendLength);
           Serial.println();
        }
        LocoNet.send( &SendPacketSensor, uiLnSendLength );    

        copyUid(mfrc522.uid.uidByte, oldUid, mfrc522.uid.size);
        
     } else { //if(!delaying)
	       uiActTime = millis();	
         if(compareUid(	mfrc522.uid.uidByte, oldUid, mfrc522.uid.size)){//same UID	
	          if((uiActTime - uiStartTime) > uiDelayTime){
	             delaying = false;
            } //if((uiActTime
         } else { //new UID
            delaying = false;
         }
     } //else 
     
     // Halt PICC
     mfrc522.PICC_HaltA();
     // Stop encryption on PCD
     mfrc522.PCD_StopCrypto1();

  } //if ( mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()){    
  LnPacket = LocoNet.receive() ;
  if( LnPacket && ((LnPacket->data[2] != ucBoardAddrLo) || (LnPacket->data[4] != ucBoardAddrHi))) { //new message sent by other
     uint8_t msgLen = getLnMsgSize(LnPacket);
     
     //Change the board & sensor addresses. Changing the board address is working
     if(msgLen == 0x10){  //XFERmessage, check if it is for me. Used to change the address
         //svStatus = sv.processMessage(LnPacket);
         
        processXferMess(LnPacket, &SendPacket);
        LocoNet.send( &SendPacket );    

        calcSenAddr();
        setMessageHeader(); //if the sensor address was changed, update the header                
     } //if(msgLen == 0x10)
  }
}

