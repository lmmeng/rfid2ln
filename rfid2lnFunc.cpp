#include <SPI.h>
#include <MFRC522.h>
#include <LocoNet.h>
#include <EEPROM.h>
#include "rfid2ln.h"

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}

/**
 * Function used to compare two RFID UIDs
 */
bool compareUid(byte *buffer1, byte *buffer2, byte bufferSize) {
    bool retVal = true;
    for (byte i = 0; i < bufferSize, retVal; i++) {
        retVal = (buffer1[i] == buffer2[i]);
    }
  return retVal;
}

/**
 * Function used to copy one RFID UID to another one
 * Maybe easier with memcpy?
 */
void copyUid (byte *buffIn, byte *buffOut, byte bufferSize) {
//    for (byte i = 0; i < bufferSize; i++) {
//        buffOut[i] = buffIn[i];
//    }

    memcpy(buffIn, buffOut, bufferSize);
    
    if(bufferSize < UID_LEN){
       for (byte i = bufferSize; i < UID_LEN; i++) {
           buffOut[i] = 0;
       }    
    }
}

/**
 * Function used to fill the constant header of a loconet message
 * The message "header" (OPC, length, message type & board address configurated only once 
 * at begining or if the address is changed over loconet
 */
void setMessageHeader(void){
    unsigned char k = 0;
    SendPacketSensor.data[0] = 0xE4; //OPC - variable length message 
    SendPacketSensor.data[1] = uiLnSendLength; //14 bytes length
    SendPacketSensor.data[2] = 0x41; //report type 
    SendPacketSensor.data[3] = ucAddrHiSen; //sensor address high
    SendPacketSensor.data[4] = ucAddrLoSen; //sensor address low 
    
    SendPacketSensor.data[uiLnSendCheckSumIdx]=0xFF;
    for(k=0; k<5;k++){
      SendPacketSensor.data[uiLnSendCheckSumIdx] ^= SendPacketSensor.data[k];
    }
}

/**
 * Loconet programming (OPC_PEER_XFER) messages decoding function.
 */
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
                    sv.writeSVStorage(SV_ADDR_USER_BASE + (ucPeerRSvIndex % 3), ucPeerRSvValue); //save the new value
                }
                cOutBuf->data[0x0B] = ucBoardAddrHi; 
                ucTempData = sv.readSVStorage(SV_ADDR_USER_BASE + (ucPeerRSvIndex % 3));
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

            ucTempData = sv.readSVStorage(SV_ADDR_USER_BASE + (ucPeerRSvIndex-3));
            if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                cOutBuf->data[0x0A] |= 0x02; //PXCTL2.1 = D6.7
            }
            cOutBuf->data[0x0C] = ucTempData & 0x7F;

            ucTempData = sv.readSVStorage(SV_ADDR_USER_BASE + (ucPeerRSvIndex-3) + 1);
            if (ucTempData & 0x80) { //msb==1 => sent in PXCTL2
                cOutBuf->data[0x0A] |= 0x04; //PXCTL2.2 = D7.7
            }
            cOutBuf->data[0x0D] = ucTempData & 0x7F;
            ucTempData = sv.readSVStorage(SV_ADDR_USER_BASE + (ucPeerRSvIndex-3) + 2);
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
 * Loconet checksumm calculation function
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

/**
 * Setup the board. Check the version and, if the Arduino wasn't used as rfid2ln board, reprogram
 * the EEPROM. 
 * If it is a rfid2ln board, read the data from EEPROM and initialise the corresponding variables
 */
void boardSetup(void){
    boolean bVersionOK = true;
    uint8_t boardVer[] = "RFID2LN V1";
    char verLen = sizeof(boardVer);
    char ch;
    for(uint8_t i = 0; i<verLen; i++){
        ch = EEPROM.read(255 - verLen + i);
        if(ch != boardVer[i]){
           bVersionOK = false;
           break;
        } 
    }
    ucBoardAddrHi = sv.readSVStorage(SV_ADDR_NODE_ID_H); //board address high
    ucBoardAddrLo = sv.readSVStorage(SV_ADDR_NODE_ID_L); //board address low

    if(bVersionOK == false){   //not the right content in eeprom
       if(bSerialOk) { //serial interface ok
          Serial.println(F("First run. Write the default values in EEPROM"));
       }      

       for(uint8_t i = 0; i<verLen; i++){
           EEPROM.write(255 - verLen + i, boardVer[i]);
       }

       ucBoardAddrHi = 1;
       ucBoardAddrLo = 88;

       sv.writeSVStorage(SV_ADDR_NODE_ID_H, ucBoardAddrHi );
       sv.writeSVStorage(SV_ADDR_NODE_ID_L, ucBoardAddrLo);
  
       sv.writeSVStorage(SV_ADDR_SERIAL_NUMBER_H, 0x56);
       sv.writeSVStorage(SV_ADDR_SERIAL_NUMBER_L, 0x78);

       ucSenType=0x0F;
       sv.writeSVStorage(SV_ADDR_USER_BASE+2, 0);
       sv.writeSVStorage(SV_ADDR_USER_BASE+1, 0);
       sv.writeSVStorage(SV_ADDR_USER_BASE, ucSenType);
    } else {
       if(bSerialOk) { //serial interface ok
          for(uint8_t i = 0; i<verLen; i++){
             Serial.print((char)boardVer[i]);
          }
          Serial.println();
          // Show some details of the loconet setup
          Serial.print(F("Board address: "));
          Serial.print(ucBoardAddrHi);
          Serial.print(F(" - "));
          Serial.println(ucBoardAddrLo);
       }
    }
}

/**
 * Recalculate the addresses printed / sent with RFID messages.
 * Needed at the begining and after the sensor address reprogramming over Loconet
 */
void calcSenAddr(void){
    // Rocrail compatible addressing
    uiAddrSenFull = 256 * (sv.readSVStorage(SV_ADDR_USER_BASE+2) & 0x0F) + 2 * sv.readSVStorage(SV_ADDR_USER_BASE+1) +
                    (sv.readSVStorage(SV_ADDR_USER_BASE+2) >> 5) + 1;

    ucAddrHiSen = (uiAddrSenFull >> 7) & 0x7F;
    ucAddrLoSen = uiAddrSenFull & 0x7F;        
    ucSenType = sv.readSVStorage(SV_ADDR_USER_BASE); //"sensor" type = in  
}

void printSensorData(void){
    Serial.print(F("Full sensor addr: "));
    Serial.println(uiAddrSenFull);
    Serial.print(F("Sensor AddrH: "));
    Serial.print(ucAddrHiSen);
    Serial.print(F(" Sensor AddrL: "));
    Serial.print(ucAddrLoSen);
    Serial.println();  
}

