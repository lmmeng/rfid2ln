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
 * SPI SS      SDA(SS)      10            53        D10        10               10
 * SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           16
 * SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           14
 * SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           15
 * 
 * Typical pin layout used - Loconet:
 * -----------------------------------------------------------------------------------------
 *             Loconet      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Signal       Uno           Mega      Nano v3    Leonardo/Micro   Pro Micro
 * -----------------------------------------------------------------------------------------
 *             RX                                              4                
 *             TX                                              6                
 *
 */

#include <SPI.h>
#include <MFRC522.h>
#include <LocoNet.h>

#define RST_PIN         9           // Configurable, see typical pin layout above
#define SS_PIN          10          // Configurable, see typical pin layout above

#define MANUF_ID        13          // DIY DCC
#define BOARD_TYPE      5           // something for sv.init

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

MFRC522::MIFARE_Key key;


    // --------------------------------------------------------
    // OPC_PEER_XFER SV_CMD's
    // --------------------------------------------------------

#define SV_CMD_WRITE            0x01    // Write 1 byte of data from D1
#define SV_CMD_READ             0x02    // Initiate read 1 byte of data into D1
#define SV_CMD_MASKED_WRITE     0x03    // Write 1 byte of masked data from D1. D2 contains the mask to be used.
#define SV_CMD_WRITE4           0x05    // Write 4 bytes of data from D1..D4
#define SV_CMD_READ4            0x06    // Initiate read 4 bytes of data into D1..D4
#define SV_CMD_DISCOVER         0x07    // Causes all devices to identify themselves by their MANUFACTURER_ID, DEVELOPER_ID, PRODUCT_ID and Serial Number
#define SV_CMD_IDENTIFY         0x08    // Causes an individual device to identify itself by its MANUFACTURER_ID, DEVELOPER_ID, PRODUCT_ID and Serial Number
#define SV_CMD_CHANGE_ADDR      0x09    // Changes the device address to the values specified in <DST_L> + <DST_H> in the device that matches the values specified in <SV_ADRL> + <SV_ADRH> + <D1>..<D4> that we in the reply to the Discover or Identify command issued previously
#define SV_CMD_RECONFIGURE      0x4F    // Initiates a device reconfiguration or reset so that any new device configuration becomes active

    // Replies
#define SV_CMDR_WRITE           0x41    // Transfers a write response in D1
#define SV_CMDR_READ            0x42    // Transfers a read response in D1
#define SV_CMDR_MASKED_WRITE    0x43    // Transfers a masked write response in D1
#define SV_CMDR_WRITE4          0x45    // Transfers a write response in D1..D4
#define SV_CMDR_READ4           0x46    // Transfers a read response in D1..D4
#define SV_CMDR_DISCOVER        0x47    // Transfers an Discover response containing the MANUFACTURER_ID, DEVELOPER_ID, PRODUCT_ID and Serial Number
#define SV_CMDR_IDENTIFY        0x48    // Transfers an Identify response containing the MANUFACTURER_ID, DEVELOPER_ID, PRODUCT_ID and Serial Number
#define SV_CMDR_CHANGE_ADDR     0x49    // Transfers a Change Address response.
#define SV_CMDR_RECONFIGURE     0x4F    // Acknowledgement immediately prior to a device reconfiguration or reset

//Other LN definitions
#define SEN_QUERY_LOW_ADDRESS   0x79    // 1017 & 0x007F - 7 bits low address for the sensors query address 1017
#define SEN_QUERY_HIGH_ADDRESS  0x07    // (1017 >> 8) & 0x07 - high address bits for the sensors query address 1017
#define LN_MESS_LEN_PEER 16

//Version
#define VER_LOW         0x01
#define VER_HIGH        0X00

LocoNetSystemVariableClass sv;
lnMsg       *LnPacket;
lnMsg       SendPacket ;
SV_STATUS   svStatus = SV_OK;
boolean     deferredProcessingNeeded = false;

unsigned char ucAddrHi = 0;
unsigned char ucAddrLo = 0;

bool compare_uid(byte *buffer1, byte *buffer2, byte bufferSize);
void setMessageHeader(void);
uint8_t processXferMess(lnMsg *LnRecMsg, lnMsg *LnSendMsg);
uint8_t lnCalcCheckSumm(uint8_t *cMessage, uint8_t cMesLen);
  
/**
 * Initialize.
 */
void setup() {
#ifdef _SER_DEBUG
    Serial.begin(115200); // Initialize serial communications with the PC
    while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
#endif

    //initialize the LocoNet interface
    LocoNet.init();
    sv.init(MANUF_ID, BOARD_TYPE, 1, 1); //to see if needed just once (saved in EEPROM)
    
    ucAddrHi = sv.readSVStorage(SV_ADDR_NODE_ID_H);
    ucAddrLo = sv.readSVStorage(SV_ADDR_NODE_ID_L);
    
    if((ucAddrHi == 0xFF) && (ucAddrLo == 0xFF)){ //eeprom empty, first run 
       ucAddrHi = 1;
       ucAddrLo = 0;
       sv.writeSVStorage(SV_ADDR_NODE_ID_H, 1 );
       sv.writeSVStorage(SV_ADDR_NODE_ID_L, 0);
  
       sv.writeSVStorage(SV_ADDR_SERIAL_NUMBER_H, 0x56);
       sv.writeSVStorage(SV_ADDR_SERIAL_NUMBER_L, 0x78);
    }
    
    SPI.begin();        // Init SPI bus
    mfrc522.PCD_Init(); // Init MFRC522 card

    // Prepare the key (used both as key A and as key B)
    // using FFFFFFFFFFFFh which is the default at chip delivery from the factory
    for (byte i = 0; i < 6; i++) {
        key.keyByte[i] = 0xFF;
    }

    // The message "header" (OPC, length, message type & board address configurated only once at begining
    // or if the address is changed over loconet

    setMessageHeader();
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
  uint16_t uiDelayTime = 200;

  if ( mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()){  
     if(!delaying){   //Avoid to many/to fast reads of the same tag
/*       
        // Show some details of the PICC (that is: the tag/card)
        Serial.print(F("Card UID:"));
        dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
        Serial.println();
*/
	uiStartTime = millis();
	delaying = true;

        SendPacket.data[10]=0;
        for(i=0, j=5; i< 5; i++, j++){
           if(mfrc522.uid.size > i){
              SendPacket.data[j] = mfrc522.uid.uidByte[i] & 0x7F; //loconet bytes haver only 7 bits;
                                                               // MSbit is transmited in the SendPacket.data[10]
              if(mfrc522.uid.uidByte[i] & 0x80){
                 SendPacket.data[10] |= 1 << i;
              }
              SendPacket.data[11] ^= SendPacket.data[j]; //calculate the checksumm
           } else {
              SendPacket.data[j] = 0;
           }        
        } //for(i=0

        SendPacket.data[11] ^= SendPacket.data[10]; //calculate the checksumm
        
        LocoNet.send( &SendPacket );        
        
     } else { //if(!delaying)
	uiActTime = millis();			
	if((uiActTime - uiStartTime) > uiDelayTime){
	   delaying = false;
        } //if((uiActTime
     } //else 
     
     // Halt PICC
     mfrc522.PICC_HaltA();
     // Stop encryption on PCD
     mfrc522.PCD_StopCrypto1();

  } //if ( mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()){    

  LnPacket = LocoNet.receive() ;
  if( LnPacket ) {
     uint8_t msgLen = getLnMsgSize(LnPacket);
#ifdef _SER_DEBUG
     Serial.print(F("Loconet rec:"));
     dump_byte_array(LnPacket.data, getLnMsgSize(LnPacket)); //
     Serial.println();
#endif

     //in this part I'm trying to change the board address. Should see if it is working 
     // directly with the library function or I should implement it 
     if(msgLen == 0x10){  //XFERmessage, check if it is for me. Used to change the address
        //sv.processMessage doesn't work. It expected special formats that are not in 
        //the loconetpersonaledition.pdf => oun implementatio => processXferMess()
        //svStatus = sv.processMessage(LnPacket);

        processXferMess(LnPacket, &SendPacket);
        LocoNet.send( &SendPacket );        
                
#ifdef _SER_DEBUG
        Serial.print("LNSV processMessage - Status: ");
        Serial.println(svStatus);
#endif    
//        deferredProcessingNeeded = (svStatus == SV_DEFERRED_PROCESSING_NEEDED);
     } //if(msgLen == 0x10)
  }

//  if(deferredProcessingNeeded){
//     deferredProcessingNeeded = (sv.doDeferredProcessing() != SV_OK);
//  }
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

bool compare_uid(byte *buffer1, byte *buffer2, byte bufferSize) {
    bool retVal = true;
    for (byte i = 0; i < bufferSize, retVal; i++) {
        retVal = (buffer1[i] == buffer2[i]);
    }
	return retVal;
}

void setMessageHeader(void){
    unsigned char k =0;
    SendPacket.data[0] = 0xE4; //OPC - variable length message 
    SendPacket.data[1] = 0x0C; //12 bytes length
    SendPacket.data[2] = 0x41; //report type 
    SendPacket.data[3] = ucAddrHi; //12 bytes length
    SendPacket.data[4] = ucAddrLo; //report type 
    
    SendPacket.data[11]=0xFF;
    for(k=0; k<5;k++){
      SendPacket.data[11] ^= SendPacket.data[k];
    }
}

uint8_t processXferMess(lnMsg *LnRecMsg, lnMsg *cOutBuf){
    
    unsigned char ucPeerRCommand = 0;
    unsigned char ucPeerRSvIndex = 0;
    unsigned char ucPeerRSvValue = 0;
    unsigned char ucTempData = 0;
    
    if ((LnRecMsg->data[3] != ucAddrLo) && (LnRecMsg->data[3] != 0)) { //no my low address and no broadcast
        return (0);
    } else if ((LnRecMsg->data[4] != ucAddrHi) && (LnRecMsg->data[4] != 0x7F)) {//not my low address and not address programming
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
            if (ucPeerRSvIndex == 0) {
                ucPeerRSvValue &= 0xFE; //LocoHDL is increasing this value with each write cycle
                cOutBuf->data[0x0B] = ucAddrHi;
                cOutBuf->data[0x0E] = 0;
            } else if (ucPeerRSvIndex == 1) { //new low_address
                ucAddrLo = ucPeerRSvValue;
                // initMessagesArray();
                cOutBuf->data[0x0B] = 0x7F;
                cOutBuf->data[0x0E] = ucPeerRSvValue;
                sv.writeSVStorage(ucPeerRSvIndex, ucPeerRSvValue); //save the new value
            } else if (ucPeerRSvIndex == 2) { //new high_address
                if (ucPeerRSvValue != 0x7F) {
                    //initMessagesArray();
                    ucAddrHi = ucPeerRSvValue;
                    sv.writeSVStorage(ucPeerRSvIndex, ucPeerRSvValue); //save the new value
                }
                cOutBuf->data[0x0B] = 0x7F;
                cOutBuf->data[0x0E] = 0x7F;
            }
            cOutBuf->data[0x0C] = 0;
            cOutBuf->data[0x0D] = 0;
        } //if (cLnBuffer[0x06] == SV_CMD_WRITE)
        
        if ((ucPeerRCommand == SV_CMD_READ) || (ucPeerRCommand == 0)) { //read command. Answer to sender
            cOutBuf->data[0x0B] = 0x01;

            ucTempData = sv.readSVStorage(ucPeerRSvIndex);
            if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                cOutBuf->data[0x0A] |= 0x02; //PXCTL2.1 = D6.7
            }
            cOutBuf->data[0x0C] = ucTempData & 0x7F;

            ucTempData = sv.readSVStorage(ucPeerRSvIndex + 1);
            if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                cOutBuf->data[0x0A] |= 0x04; //PXCTL2.2 = D7.7
            }
            cOutBuf->data[0x0D] = ucTempData & 0x7F;
            ucTempData = sv.readSVStorage(ucPeerRSvIndex + 2);
            if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                cOutBuf->data[0x0A] |= 0x08; //PXCTL2.3 = D8.7
            }
            cOutBuf->data[0x0E] = ucTempData & 0x7F;
        } //if (cLnBuffer[0x06] == SV_CMD_READ)

        cOutBuf->data[0x02] = ucAddrLo; // src low address;
        cOutBuf->data[0x03] = LnRecMsg->data[0x02]; //dest low addres == received src low address;
        cOutBuf->data[0x04] = ucAddrHi;
        cOutBuf->data[0x05] = VER_HIGH; //unsigned char pxct1; (bit 3 = MSBit(b7) version)
        cOutBuf->data[0x06] = ucPeerRCommand; //0x02;  //unsigned char cmd;
        cOutBuf->data[0x07] = ucPeerRSvIndex;
        cOutBuf->data[0x08] = VER_LOW; //LSBits version
        cOutBuf->data[0x09] = 0x7B;

        cOutBuf->data[0x0F] = lnCalcCheckSumm(cOutBuf->data, LN_MESS_LEN_PEER);
    }

    return 1;
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


