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

//#define _SER_DEBUG


#define MANUF_ID        13          /* DIY DCC*/
#define BOARD_TYPE      5           /* something for sv.init*/

#define NR_OF_PORTS     2

#define UNO_LM 

#if ARDUINO >= 10500 //the board naming scheme is supported from Arduino 1.5.0
 #if (defined(ARDUINO_AVR_UNO) && !defined(UNO_LM)) || defined(ARDUINO_AVR_NANO)
  #define LN_TX_PIN       7           /* Arduino Pin used as Loconet Tx; Rx Pin is always the ICP Pin */
  #define RST_PIN         9           /* Configurable, see typical pin layout above*/
  #define SS_1_PIN       10           /* Configurable, see typical pin layout above*/   
  #if NR_OF_PORTS >= 2  
     #define SS_2_PIN     2           /* Configurable, see typical pin layout above*/   
  #endif     
#elif defined(ARDUINO_AVR_LEONARDO) || defined(UNO_LM) 
  #define LN_TX_PIN       6           /* Arduino Pin used as Loconet Tx; Rx Pin is always the ICP Pin */
  #define RST_PIN         9           /* Configurable, see typical pin layout above*/
  #define SS_1_PIN        5           /* Configurable, see typical pin layout above*/   
  #if NR_OF_PORTS >= 2  
     #define SS_2_PIN     2           /* Configurable, see typical pin layout above*/   
  #endif     
 #endif
#else //older arduino IDE => initialising each board as it is used. I'm using Leonardo
  #define LN_TX_PIN       6           /* Arduino Pin used as Loconet Tx; Rx Pin is always the ICP Pin */
  #define RST_PIN         9           /* Configurable, see typical pin layout above*/
  #define SS_1_PIN         5           /* Configurable, see typical pin layout above*/   
  #if NR_OF_PORTS >= 2  
     #define SS_2_PIN         2           /* Configurable, see typical pin layout above*/   
  #endif     
#endif 

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

MFRC522 mfrc522_1(SS_1_PIN, RST_PIN);   // Create the first MFRC522 instance.
#if NR_OF_PORTS >= 2  
   MFRC522 mfrc522_2(SS_2_PIN, RST_PIN);   // Create the second MFRC522 instance. 
#endif     


MFRC522::MIFARE_Key key;


LocoNetSystemVariableClass sv;
lnMsg       *LnPacket;
lnMsg       SendPacket ;
lnMsg       SendPacketSensor ;

SV_STATUS   svStatus = SV_OK;
boolean     deferredProcessingNeeded = false;

uint8_t ucBoardAddrHi = 1;  //board address high; always 1
uint8_t ucBoardAddrLo = 88;  //board address low; default 88

uint8_t ucAddrHiSen[NR_OF_PORTS];    //sensor address high
uint8_t ucAddrLoSen[NR_OF_PORTS];    //sensor address low
uint8_t ucSenType[NR_OF_PORTS] = {0x0F}; //input
uint16_t uiAddrSenFull[NR_OF_PORTS];

bool compareUid(byte *buffer1, byte *buffer2, byte bufferSize);
void copyUid(byte *buffIn, byte *buffOut, byte bufferSize);
void setMessageHeader(uint8_t);
uint8_t processXferMess(lnMsg *LnRecMsg, lnMsg *LnSendMsg);
uint8_t lnCalcCheckSumm(uint8_t *cMessage, uint8_t cMesLen);
uint8_t uiLnSendCheckSumIdx = 13;
uint8_t uiLnSendLength = 14; //14 bytes
uint8_t uiLnSendMsbIdx = 12;
uint8_t uiStartChkSen;

uint8_t oldUid[NR_OF_PORTS][UID_LEN] = {0};

boolean bSerialOk=false;
  
/**
 * Initialize.
 */
void setup() {
//#ifdef _SER_DEBUG
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
    }
//#endif

    //initialize the LocoNet interface
    LocoNet.init(LN_TX_PIN); //Always use the explicit naming of the Tx Pin to avoid confusions 
    sv.init(MANUF_ID, BOARD_TYPE, 1, 1); //to see if needed just once (saved in EEPROM)

    
    ucBoardAddrHi = sv.readSVStorage(SV_ADDR_NODE_ID_H); //board address high
    ucBoardAddrLo = sv.readSVStorage(SV_ADDR_NODE_ID_L); //board address low

    if((ucBoardAddrHi == 0xFF) && (ucBoardAddrLo == 0xFF)){ //eeprom empty, first run 
       ucBoardAddrHi = 1;
       ucBoardAddrLo = 88;
          
       sv.writeSVStorage(SV_ADDR_NODE_ID_H, ucBoardAddrHi );
       sv.writeSVStorage(SV_ADDR_NODE_ID_L, ucBoardAddrLo);
  
       sv.writeSVStorage(SV_ADDR_SERIAL_NUMBER_H, 0x56);
       sv.writeSVStorage(SV_ADDR_SERIAL_NUMBER_L, 0x78);

       int iSenAddr = 0;
       for(int i=0; i<NR_OF_PORTS; i++){
          iSenAddr = SV_ADDR_USER_BASE + 3 + 3*i;         
          sv.writeSVStorage(iSenAddr+2, i); //should see how send the rocrail the address 2
          sv.writeSVStorage(iSenAddr+1, 0);
          sv.writeSVStorage(iSenAddr, ucSenType[i]);
       }
    }

    // Rocrail compatible addressing
    uint8_t iSenAddr = 0;
    for(uint8_t i=0; i<NR_OF_PORTS; i++){
       iSenAddr = SV_ADDR_USER_BASE + 3 + 3*i;         
       uiAddrSenFull[i] = 256 * (sv.readSVStorage(iSenAddr+2) & 0x0F) + 2 * sv.readSVStorage(iSenAddr+1) +
                    (sv.readSVStorage(iSenAddr+2) >> 5) - 1;

       ucAddrHiSen[i] = (uiAddrSenFull[i] >> 7) & 0x7F;
       ucAddrLoSen[i] = uiAddrSenFull[i] & 0x7F;        
       ucSenType[i] = sv.readSVStorage(iSenAddr); //"sensor" type = in
       setMessageHeader(i);
   }
    uiStartChkSen = SendPacketSensor.data[uiLnSendCheckSumIdx];
    
    SPI.begin();        // Init SPI bus
    
    mfrc522_1.PCD_Init(); // Init first MFRC522 card
#if NR_OF_PORTS >= 2
    mfrc522_2.PCD_Init(); // Init 2nd MFRC522 card
#endif
    // Prepare the key (used both as key A and as key B)
    // using FFFFFFFFFFFFh which is the default at chip delivery from the factory
    for (byte i = 0; i < 6; i++) {
        key.keyByte[i] = 0xFF;
    }

    // The message "header" (OPC, length, message type & board address configurated only once at begining
    // or if the address is changed over loconet


//#ifdef _SER_DEBUG
    if(bSerialOk){
        // Show some details of the loconet setup
        Serial.print(F("Board address: "));
        Serial.print(ucBoardAddrHi);
        Serial.print(F(" - "));
        Serial.println(ucBoardAddrLo);
        
        for(int i=0; i<NR_OF_PORTS; i++){
            Serial.print(F("Full sensor ")); 
            Serial.print(i);
            Serial.print(F(" addr: "));
            Serial.println(uiAddrSenFull[i]);
            Serial.print(F("Sensor AddrH: "));
            Serial.print(ucAddrHiSen[i]);
            Serial.print(F(" Sensor AddrL: "));
            Serial.print(ucAddrLoSen[i]);
            Serial.println();
        }
    }
//#endif
    
}

/**
 * Main loop.
 */
void loop() {
  SV_STATUS svStatus;
  unsigned long uiStartTime;
  unsigned long uiActTime;
  unsigned char i=0;
  unsigned char j=0;  
  uint16_t uiDelayTime = 1000;

  if ( mfrc522_1.PICC_IsNewCardPresent() && mfrc522_1.PICC_ReadCardSerial()){  
     static bool delaying = false;
     if(!delaying){   //Avoid to many/to fast reads of the same tag
        // Show some details of the PICC (that is: the tag/card)
        if(bSerialOk){
           Serial.print(F("Card UID:"));
           dump_byte_array(mfrc522_1.uid.uidByte, mfrc522_1.uid.size);
           Serial.println();
        }

#ifdef _SER_DEBUG
        // Show some details of the loconet setup
        Serial.print(F("Full sen addr: "));
        Serial.print(uiAddrSenFull[0]);
        Serial.print(F("Sensor AddrH: "));
        Serial.print(ucAddrHiSen[0]);
        Serial.print(F(" Sensor AddrL: "));
        Serial.print(ucAddrLoSen[0]);
        Serial.println();
#endif

	      uiStartTime = millis();
	      delaying = true;

        SendPacketSensor.data[uiLnSendCheckSumIdx]= uiStartChkSen; //start with header check summ
        SendPacketSensor.data[uiLnSendMsbIdx]=0; //clear the byte for the ms bits
        for(i=0, j=5; i< UID_LEN; i++, j++){
           if(mfrc522_1.uid.size > i){
              SendPacketSensor.data[j] = mfrc522_1.uid.uidByte[i] & 0x7F; //loconet bytes haver only 7 bits;
                                                               // MSbit is transmited in the SendPacket.data[10]
              if(mfrc522_1.uid.uidByte[i] & 0x80){
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

        setMessageHeader(0); //if the sensor address was changed, update the header   
        LocoNet.send( &SendPacketSensor, uiLnSendLength );    

        copyUid(mfrc522_1.uid.uidByte, oldUid[0], mfrc522_1.uid.size);
        
     } else { //if(!delaying)
	       uiActTime = millis();	
         if(compareUid(	mfrc522_1.uid.uidByte, oldUid[0], mfrc522_1.uid.size)){//same UID	
	          if((uiActTime - uiStartTime) > uiDelayTime){
	             delaying = false;
            } //if((uiActTime
         } else { //new UID
            delaying = false;
         }
     } //else 
     
     // Halt PICC
     mfrc522_1.PICC_HaltA();
     // Stop encryption on PCD
     mfrc522_1.PCD_StopCrypto1();

  } //if ( mfrc522_1.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()){
      
#if NR_OF_PORTS >= 2
  if ( mfrc522_2.PICC_IsNewCardPresent() && mfrc522_2.PICC_ReadCardSerial()){  
     static bool delaying = false;
     if(!delaying){   //Avoid to many/to fast reads of the same tag
        // Show some details of the PICC (that is: the tag/card)
        if(bSerialOk){
           Serial.print(F("Card UID:"));
           dump_byte_array(mfrc522_2.uid.uidByte, mfrc522_2.uid.size);
           Serial.println();
        }

#ifdef _SER_DEBUG
        // Show some details of the loconet setup
        Serial.print(F("Full sen addr: "));
        Serial.print(uiAddrSenFull[1]);
        Serial.print(F("Sensor AddrH: "));
        Serial.print(ucAddrHiSen[1]);
        Serial.print(F(" Sensor AddrL: "));
        Serial.print(ucAddrLoSen[1]);
        Serial.println();
#endif

        uiStartTime = millis();
        delaying = true;

        SendPacketSensor.data[uiLnSendCheckSumIdx]= uiStartChkSen; //start with header check summ
        SendPacketSensor.data[uiLnSendMsbIdx]=0; //clear the byte for the ms bits
        for(i=0, j=5; i< UID_LEN; i++, j++){
           if(mfrc522_2.uid.size > i){
              SendPacketSensor.data[j] = mfrc522_2.uid.uidByte[i] & 0x7F; //loconet bytes haver only 7 bits;
                                                               // MSbit is transmited in the SendPacket.data[10]
              if(mfrc522_2.uid.uidByte[i] & 0x80){
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

        setMessageHeader(1); //if the sensor address was changed, update the header   
        LocoNet.send( &SendPacketSensor, uiLnSendLength );    

        copyUid(mfrc522_2.uid.uidByte, oldUid[1], mfrc522_2.uid.size);
        
     } else { //if(!delaying)
         uiActTime = millis();  
         if(compareUid( mfrc522_2.uid.uidByte, oldUid[1], mfrc522_2.uid.size)){//same UID 
            if((uiActTime - uiStartTime) > uiDelayTime){
               delaying = false;
            } //if((uiActTime
         } else { //new UID
            delaying = false;
         }
     } //else 
     
     // Halt PICC
     mfrc522_2.PICC_HaltA();
     // Stop encryption on PCD
     mfrc522_2.PCD_StopCrypto1();

  } //if ( mfrc522_2.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()){
#endif

  LnPacket = LocoNet.receive() ;
  if( LnPacket && ((LnPacket->data[2] != ucBoardAddrLo) || (LnPacket->data[4] != ucBoardAddrHi))) { //new message sent by other
     uint8_t msgLen = getLnMsgSize(LnPacket);
     
     //Change the board & sensor addresses. Changing the board address is working
     if(msgLen == 0x10){  //XFERmessage, check if it is for me. Used to change the address
         //svStatus = sv.processMessage(LnPacket);
         
        processXferMess(LnPacket, &SendPacket);
        LocoNet.send( &SendPacket );    

//        uiAddrSenFull = 256 * (sv.readSVStorage(SV_ADDR_USER_BASE+2) & 0x0F) + 2 * sv.readSVStorage(SV_ADDR_USER_BASE + 1) +
//                        (sv.readSVStorage(SV_ADDR_USER_BASE+2) >> 5) + 1;

//        ucAddrHiSen = (uiAddrSenFull >> 7) & 0x7F;
//        ucAddrLoSen = uiAddrSenFull & 0x7F;

        // Rocrail compatible addressing
        uint8_t iSenAddr = 0;
        for(uint8_t i=0; i<NR_OF_PORTS; i++){
           iSenAddr = SV_ADDR_USER_BASE + 3 + 3*i;         
           uiAddrSenFull[i] = 256 * (sv.readSVStorage(iSenAddr+2) & 0x0F) + 2 * sv.readSVStorage(iSenAddr+1) +
                    (sv.readSVStorage(iSenAddr+2) >> 5) - 1;

           ucAddrHiSen[i] = (uiAddrSenFull[i] >> 7) & 0x7F;
           ucAddrLoSen[i] = uiAddrSenFull[i] & 0x7F;        
//           setMessageHeader(i); //if the sensor address was changed, update the header   

           Serial.print(F("Full sen addr: "));
           Serial.print(uiAddrSenFull[i]);
           Serial.print(F(" Sensor AddrH: "));
           Serial.print(ucAddrHiSen[i]);
           Serial.print(F(" Sensor AddrL: "));
           Serial.print(ucAddrLoSen[i]);
           Serial.println();
                       
        }
     } //if(msgLen == 0x10)
  }
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}

bool compareUid(byte *buffer1, byte *buffer2, byte bufferSize) {
    bool retVal = true;
    for (byte i = 0; i < bufferSize, retVal; i++) {
        retVal = (buffer1[i] == buffer2[i]);
    }
	return retVal;
}

void copyUid (byte *buffIn, byte *buffOut, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        buffOut[i] = buffIn[i];
    }
    if(bufferSize < UID_LEN){
       for (byte i = bufferSize; i < UID_LEN; i++) {
           buffOut[i] = 0;
       }    
    }
}

void setMessageHeader(uint8_t port){
    unsigned char k = 0;
    SendPacketSensor.data[0] = 0xE4; //OPC - variable length message 
    SendPacketSensor.data[1] = uiLnSendLength; //14 bytes length
    SendPacketSensor.data[2] = 0x41; //report type 
    SendPacketSensor.data[3] = ucAddrHiSen[port]; //sensor address high
    SendPacketSensor.data[4] = ucAddrLoSen[port]; //sensor address low 
    
    SendPacketSensor.data[uiLnSendCheckSumIdx]=0xFF;
    for(k=0; k<5;k++){
      SendPacketSensor.data[uiLnSendCheckSumIdx] ^= SendPacketSensor.data[k];
    }
}

uint8_t processXferMess(lnMsg *LnRecMsg, lnMsg *cOutBuf){
    
    unsigned char ucPeerRCommand = 0;
    unsigned char ucPeerRSvIndex = 0;
    unsigned char ucPeerRSvValue = 0;
    unsigned char ucTempData = 0;
    
    if ((LnRecMsg->data[3] != ucBoardAddrLo) && (LnRecMsg->data[3] != 0)) { //no my low address and no broadcast
        return (0);
    } else if ((LnRecMsg->data[4] != ucBoardAddrHi) && (LnRecMsg->data[4] != 0x7F)) {//not my low address and not address programming
        return (0);
    } else {//message for me
        cOutBuf->data[0x00] = 0xE5; //allways PEER
        cOutBuf->data[0x01] = 0x10; //always 16 bytes long
        cOutBuf->data[0x0A] = 0; //clear the cOutBuf[0x0A];

        if (LnRecMsg->data[0x05] & 0x08) //MSbit in reg5->3
            LnRecMsg->data[0x09] |= 0x80;
        if (LnRecMsg->data[0x05] & 0x04) //MSbit in reg5->2
            LnRecMsg->data[0x08] |= 0x80;
        if (LnRecMsg->data[0x05] & 0x02) //MSbit in reg5->1
            LnRecMsg->data[0x07] |= 0x80;
        if (LnRecMsg->data[0x05] & 0x01) //MSbit in reg5->0
            LnRecMsg->data[0x06] |= 0x80;

        if (LnRecMsg->data[0x0A] & 0x08) //MSbit in regA->3
            LnRecMsg->data[0x0E] |= 0x80;
        if (LnRecMsg->data[0x0A] & 0x04) //MSbit in regA->2
            LnRecMsg->data[0x0D] |= 0x80;
        if (LnRecMsg->data[0x0A] & 0x02) //MSbit in regA->1
            LnRecMsg->data[0x0C] |= 0x80;
        if (LnRecMsg->data[0x0A] & 0x01) //MSbit in regA->0
            LnRecMsg->data[0x0B] |= 0x80;

        ucPeerRCommand = LnRecMsg->data[0x06];
        ucPeerRSvIndex = LnRecMsg->data[0x07];
        ucPeerRSvValue = LnRecMsg->data[0x09];

        if (ucPeerRCommand == SV_CMD_WRITE) { //write command. Save the new data and answer to sender
            if (ucPeerRSvIndex == 0) { //board address high
                ucPeerRSvValue &= 0xFE; //LocoHDL is increasing this value with each write cycle
                cOutBuf->data[0x0B] = ucBoardAddrHi;
                cOutBuf->data[0x0E] = 0;
            } else if (ucPeerRSvIndex == 1) { //new low_address
                ucBoardAddrLo = ucPeerRSvValue;
                // initMessagesArray();
                cOutBuf->data[0x0B] = 0x7F;
                ucBoardAddrLo = ucPeerRSvValue;
                cOutBuf->data[0x0E] = ucPeerRSvValue;
                sv.writeSVStorage(SV_ADDR_NODE_ID_L, ucPeerRSvValue); //save the new value
            } else if (ucPeerRSvIndex == 2) { //new high_address
                if (ucPeerRSvValue != 0x7F) {
                    //initMessagesArray();
                    ucBoardAddrHi = ucPeerRSvValue;
                    sv.writeSVStorage(SV_ADDR_NODE_ID_H, ucPeerRSvValue); //save the new value
                }
                cOutBuf->data[0x0B] = 0x7F;
                cOutBuf->data[0x0E] = 0x7F;
            } else if (ucPeerRSvIndex < (NR_OF_PORTS * 3 + 3)) { //nr_of_ports (1) * 3 register starting with the address 3
                if ((ucPeerRSvIndex % 3) != 0) { // do not change the type (leave it as IN)
                    sv.writeSVStorage(SV_ADDR_USER_BASE + ucPeerRSvIndex, ucPeerRSvValue); //save the new value
                }
                cOutBuf->data[0x0B] = ucBoardAddrHi; 
                ucTempData = sv.readSVStorage(SV_ADDR_USER_BASE + ucPeerRSvIndex);
                if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                   cOutBuf->data[0x0A] |= 0x08; //PXCTL2.3 = D8.7
                }
                cOutBuf->data[0x0E] = ucTempData & 0x7F;
            } //if (ucPeerRSvIndex < (NR_OF_PORTS * 3 + 3))
            cOutBuf->data[0x0C] = 0;
            cOutBuf->data[0x0D] = 0;
        } //if (cLnBuffer[0x06] == SV_CMD_WRITE)
        
        if ((ucPeerRCommand == SV_CMD_READ) || (ucPeerRCommand == 0)) { //read command. Answer to sender
            cOutBuf->data[0x0B] = 0x01;

            ucTempData = sv.readSVStorage(SV_ADDR_USER_BASE + ucPeerRSvIndex);
            if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                cOutBuf->data[0x0A] |= 0x02; //PXCTL2.1 = D6.7
            }
            cOutBuf->data[0x0C] = ucTempData & 0x7F;

            ucTempData = sv.readSVStorage(SV_ADDR_USER_BASE + ucPeerRSvIndex + 1);
            if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                cOutBuf->data[0x0A] |= 0x04; //PXCTL2.2 = D7.7
            }
            cOutBuf->data[0x0D] = ucTempData & 0x7F;
            ucTempData = sv.readSVStorage(SV_ADDR_USER_BASE + ucPeerRSvIndex + 2);
            if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                cOutBuf->data[0x0A] |= 0x08; //PXCTL2.3 = D8.7
            }
            cOutBuf->data[0x0E] = ucTempData & 0x7F;
        } //if (cLnBuffer[0x06] == SV_CMD_READ)

        cOutBuf->data[0x02] = ucBoardAddrLo; // src low address;
        cOutBuf->data[0x03] = LnRecMsg->data[0x02]; //dest low addres == received src low address;
        cOutBuf->data[0x04] = ucBoardAddrHi;
        cOutBuf->data[0x05] = VER_HIGH; //unsigned char pxct1; (bit 3 = MSBit(b7) version)
        cOutBuf->data[0x06] = ucPeerRCommand; //0x02;  //unsigned char cmd;
        cOutBuf->data[0x07] = ucPeerRSvIndex;
        cOutBuf->data[0x08] = VER_LOW; //LSBits version
        cOutBuf->data[0x09] = 0x7B;

        cOutBuf->data[0x0F] = lnCalcCheckSumm(cOutBuf->data, LN_MESS_LEN_PEER);
    }

    return 1;  //should put the right here
}

/**********char lnCalcCheckSumm(...)**********************
 *
 *
 ******************************/
uint8_t lnCalcCheckSumm(uint8_t *cMessage, uint8_t cMesLen) {
    unsigned char ucIdx = 0;
    char cLnCheckSum = 0;

    for (ucIdx = 0; ucIdx < cMesLen - 1; ucIdx++) //check summ is the last byte of the message
    {
        cLnCheckSum ^= cMessage[ucIdx];
    }

    return (~cLnCheckSum);
}


