
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
    for (byte i = 0; i < bufferSize; i++) {
      if(buffer1[i] != buffer2[i]){
        return false;
      }
    }
  return true;
}

/**
 * Function used to copy one RFID UID to another one
 * Maybe easier with memcpy?
 */
void copyUid (byte *buffIn, byte *buffOut, byte bufferSize) {
//    memcpy(buffIn, buffOut, bufferSize);
    for (byte i = 0; i < bufferSize; i++) {
        buffOut[i] = buffIn[i];
    }    
    
    if(bufferSize < UID_LEN){
       for (byte i = bufferSize; i < UID_LEN; i++) {
           buffOut[i] = 0;
       }    
    }
}

void setMessageHeader(uint8_t port, uint8_t index){
    unsigned char k = 0;
    SendPacketSensor[index].data[0] = 0xE4; //OPC - variable length message 
    SendPacketSensor[index].data[1] = uiLnSendLength; //14 bytes length
    SendPacketSensor[index].data[2] = 0x41; //report type 
    SendPacketSensor[index].data[3] = ucAddrHiSen[port]; //sensor address high
    SendPacketSensor[index].data[4] = ucAddrLoSen[port]; //sensor address low 
    
    SendPacketSensor[index].data[uiLnSendCheckSumIdx]=0xFF;
    for(k=0; k<5;k++){
      SendPacketSensor[index].data[uiLnSendCheckSumIdx] ^= SendPacketSensor[index].data[k];
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
            } else if (ucPeerRSvIndex < (TOTAL_NR_OF_PORTS * 3 + 3)) { //nr_of_ports (1) * 3 register starting with the address 3
                sv.writeSVStorage(SV_ADDR_USER_BASE + ucPeerRSvIndex, ucPeerRSvValue); //save the new value
                if ((ucPeerRSvIndex % 3) == 0) { // port type. If output, increase the total number of outputs
                   if(ucPeerRSvValue == 0x10){
                      bUpdateOutputs = true; //activate the outputs structure update 
                      outputs[outsNr].idx = ucPeerRSvIndex;
                      outsNr++;
                   }
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

/**
 * Setup the board. Check the version and, if the Arduino wasn't used as rfid2ln board, reprogram
 * the EEPROM. 
 * If it is a rfid2ln board, read the data from EEPROM and initialise the corresponding variables
 */
void boardSetup(void){
    boolean bVersionOK = true;
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

       int iSenAddr = 0;
       for(int i=0; i<NR_OF_RFID_PORTS; i++){
          ucSenType[i]=0x0F;
          iSenAddr = SV_ADDR_USER_BASE + 3 + 3*i;         
          sv.writeSVStorage(iSenAddr+2, i*0x20); //1 for port1, 2 for port 2
          sv.writeSVStorage(iSenAddr+1, 0);
          sv.writeSVStorage(iSenAddr, ucSenType[i]);
       }
    } else {
       if(bSerialOk) { //serial interface ok
          for(uint8_t i = 0; i<verLen; i++){
             Serial.print((char)boardVer[i]);
          }
          Serial.println();
          Serial.print(F("Total nr. of RFID readers: "));
          Serial.println(NR_OF_RFID_PORTS);
#if USE_INTERRUPTS          
          Serial.println(F("Using interrupts"));
#else
          Serial.println(F("Using polling"));
#endif          
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
void calcSenAddr(uint8_t port){
    uint8_t iSenAddr = 0;
    
       iSenAddr = SV_ADDR_USER_BASE + 3 + 3*port;         
       uiAddrSenFull[port] = 256 * (sv.readSVStorage(iSenAddr+2) & 0x0F) + 2 * sv.readSVStorage(iSenAddr+1) +
                    (sv.readSVStorage(iSenAddr+2) >> 5) + 1;

       ucAddrHiSen[port] = (uiAddrSenFull[port] >> 7) & 0x7F;
       ucAddrLoSen[port] = uiAddrSenFull[port] & 0x7F;        
       ucSenType[port] = sv.readSVStorage(iSenAddr); //"sensor" type = in
       setMessageHeader(port, port);
}

void printSensorData(uint8_t port){
       Serial.print(F("Full sensor addr: "));
       Serial.println(uiAddrSenFull[port]);
       Serial.print(F("Sensor AddrH: "));
       Serial.print(ucAddrHiSen[port]);
       Serial.print(F(" Sensor AddrL: "));
       Serial.print(ucAddrLoSen[port]);
       Serial.println();  
}

/*
 * The function to decode the received ln message
 */
void lnDecodeMessage(lnMsg *LnPacket)
{
    uint8_t msgLen = getLnMsgSize(LnPacket);
     
    //Change the board & sensor addresses. 
    if(msgLen == 0x10){  //XFERmessage, check if it is for me. Used to change the addresses
      if((LnPacket->data[3] == ucBoardAddrLo) || (LnPacket->data[3] == 0)){ //my low address or query
        if((LnPacket->data[4] == ucBoardAddrHi) || (LnPacket->data[4] == 0x7F)){ ////my high address or query
           //svStatus = sv.processMessage(LnPacket);
         
           processXferMess(LnPacket, &SendPacket);
        
           /*5 sec timeout.*/
           LN_STATUS lnSent = LocoNet.send( &SendPacket, LN_BACKOFF_MAX - (ucBoardAddrLo % 10) );   //trying to differentiate the ln answer time   
        
          // Rocrail compatible addressing
          for (uint8_t i = 0; i < NR_OF_RFID_PORTS; i++) {
            calcSenAddr(i);

#ifdef _SER_DEBUG
            if (bSerialOk) {
              printSensorData(i);
            }
#endif
	  }//for(uint8_t i
        } //if(LnPacket->data[4]               
      } //if(LnPacket->data[3]
    } //if(msgLen == 0x10)
}

#if USE_INTERRUPTS
/*
 * MFRC522 interrupt serving routines
 */
void readCard1(uint8_t){
   bNewInt[0] = true;
}

void readCard2(uint8_t){
   bNewInt[1] = true;
}
/*
 * The function sending to the MFRC522 the needed commands to activate the reception
 */
void activateRec(MFRC522 mfrc522){
    mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg,mfrc522.PICC_CMD_REQA);
    mfrc522.PCD_WriteRegister(mfrc522.CommandReg,mfrc522.PCD_Transceive);  
    mfrc522.PCD_WriteRegister(mfrc522.BitFramingReg, 0x87);    
}

/*
 * The function to clear the pending interrupt bits after interrupt serving routine
 */
void clearInt(MFRC522 mfrc522){
   mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg,0x7F);
}
#endif
