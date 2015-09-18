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

LocoNetSystemVariableClass sv;
lnMsg       *LnPacket;
lnMsg       SendPacket ;
SV_STATUS   svStatus = SV_OK;
boolean     deferredProcessingNeeded = false;

unsigned char ucAddrHi = 0;
unsigned char ucAddrLo = 0;

bool compare_uid(byte *buffer1, byte *buffer2, byte bufferSize);
void setMessageHeader(void);
  
/**
 * Initialize.
 */
void setup() {
    Serial.begin(115200); // Initialize serial communications with the PC
    while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)

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
     if(!delaying){
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
#if 0
     Serial.print(F("Loconet rec:"));
     dump_byte_array(LnPacket->data, getLnMsgSize(LnPacket)); //
     Serial.println();
#endif

     //in this part I'm trying to change the board address. Should see if it is working 
     // directly with the library function or I should implement it 
     if(msgLen == 0x10){  //XFERmessage, check if it is for me. Used to change the address
        svStatus = sv.processMessage(LnPacket);
        Serial.print("LNSV processMessage - Status: ");
        Serial.println(svStatus);
    
        deferredProcessingNeeded = (svStatus == SV_DEFERRED_PROCESSING_NEEDED);
     } //if(msgLen == 0x10)
  }

  if(deferredProcessingNeeded){
     deferredProcessingNeeded = (sv.doDeferredProcessing() != SV_OK);
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
