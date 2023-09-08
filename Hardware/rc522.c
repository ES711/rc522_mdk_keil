#include "rc522.h"
#include "stdio.h"
#include "spi.h"
#include "string.h"
#define SDA_LOW HAL_GPIO_WritePin(GPIOB, RC522_SDA_Pin, 0);
#define SDA_HIGH HAL_GPIO_WritePin(GPIOB, RC522_SDA_Pin, 1);


void RC522_Init(){
	HAL_GPIO_WritePin(GPIOB, RC522_RST_Pin, 1);
	SDA_LOW;
	HAL_SPI_Transmit(&hspi2, (uint8_t *)0xcc, sizeof((uint8_t *)0xcc), 0xFF);
	SDA_HIGH;
	resetRC522();
	
	writeRsgister(MFRC522_REG_T_MODE, 0x8D);
	writeRsgister(MFRC522_REG_T_PRESCALER, 0x3E);
	writeRsgister(MFRC522_REG_T_RELOAD_L, 0x30);
	writeRsgister(MFRC522_REG_T_RELOAD_H, 0);

	//writeRsgister(MFRC522_REG_RF_CFG, 0x70);
	writeRsgister(MFRC522_REG_TX_AUTO, 0x40);
	writeRsgister(MFRC522_REG_MODE, 0x3D);
	
	//set iso14443_a
	clearRegister(MFRC522_REG_STATUS2, 0x08);
	writeRsgister(MFRC522_REG_RX_SELL, 0x86);
	writeRsgister(MFRC522_REG_RF_CFG, 0x7F);
	HAL_Delay(1);
	AntennaON();
	
	printf("init sucess\t\n");
}


//RC522_Status getSerialNum(uint8_t* cardSerialNum){
//	RC522_Status status;
//	status = RC522_Request(PICC_REQALL, cardSerialNum);
//	if(status == MI_OK){
//		status = anticoll(cardSerialNum);
//		//halt();
//		return status;
//	}
//	else{
//		return status;
//	}
//}

uint8_t SPI_Send(uint8_t txData){
	//TXE==1 >> txBuffer clear
	while(SPI_CHECK_FLAG(SPI2->SR, SPI_FLAG_TXE) == 0);
	//write DR register == write txBuffer == send data
	SPI2->DR = txData;
	//RXNE == 1 >> rxBuffer has data
	while(SPI_CHECK_FLAG(SPI2->SR, SPI_FLAG_RXNE) == 0);
	//read DR register == read rxBuffer
	return SPI2->DR;
}

void writeRsgister(uint8_t addr, uint8_t cmd){
	uint8_t regAddr;
	regAddr = (addr << 1)&0x7E;
	SDA_LOW;
	SPI_Send(regAddr);
	SPI_Send(cmd);
	SDA_HIGH;
}

uint8_t readRegister(uint8_t addr){
	uint8_t regAddr;
	uint8_t result;
	SDA_LOW;
	regAddr = ((addr << 1)&0x7E)|0x80;
	SPI_Send(regAddr);
	result = SPI_Send(0x00);
	SDA_HIGH;
	return result;
}

//set register to spcefic value
void setRegister(uint8_t addr, uint8_t mask){
	uint8_t tmp = readRegister(addr);
	writeRsgister(addr, tmp | mask);
}

void clearRegister(uint8_t addr, uint8_t mask){
	uint8_t tmp = readRegister(addr);
	writeRsgister(addr, tmp&(~mask));
}

void AntennaOFF(){
	clearRegister(MFRC522_REG_TX_CONTROL, 0x03);
}

void AntennaON(){
	uint8_t tmp;
	tmp = readRegister(MFRC522_REG_TX_CONTROL);
	if(!(tmp&0x03)){
		setRegister(MFRC522_REG_TX_CONTROL, 0x03);
	}
	printf("antenna on\t\n");
}

void resetRC522(){
	writeRsgister(MFRC522_REG_COMMAND, PCD_RESETPHASE);
	printf("resst rc522\t\n");
}

RC522_Status CMDtoCard(uint8_t cmd, uint8_t* txData, 
uint8_t txLen, uint8_t* rxData, uint32_t* rxLen){
	RC522_Status status = MI_ERROR;
	uint8_t irqEn = 0x00;
	uint8_t waitIRq = 0x00;
	uint8_t lastBits;
	uint8_t n;
	uint32_t i;
	
	switch(cmd){
		case PCD_AUTHENT:{
			irqEn = 0x12;
			waitIRq = 0x10;
			break;
		}
		case PCD_TRANSCEIVE:{
			irqEn = 0x77;
			waitIRq = 0x30;
		}
		default:
			break;
	}
	
	writeRsgister(MFRC522_REG_COMM_IE_N, irqEn|0x80);
	clearRegister(MFRC522_REG_COMM_IRQ, 0x80);
	setRegister(MFRC522_REG_FIFO_LEVEL, 0x80);
	writeRsgister(MFRC522_REG_COMMAND, PCD_IDLE);
	
	//write data into rc522 fifo buffer
	for(i=0;i<txLen;i++){
		writeRsgister(MFRC522_REG_FIFO_DATA, txData[i]);
	}
	
	//execute cmd
	writeRsgister(MFRC522_REG_COMMAND, cmd);
	
	if(cmd == PCD_TRANSCEIVE){
		setRegister(MFRC522_REG_BIT_FRAMING, 0x80);
	}
	
	//i = 200000000;
	do{
		n = readRegister(MFRC522_REG_COMM_IRQ);
		//i--;
	}while(!(n&0x01) && !(n&waitIRq));
	//while((i!=0) && !(n&0x01) && !(n&waitIRq));
	
	clearRegister(MFRC522_REG_COMM_IRQ, 0x80);
	
	if(i!=0){
		if(!(readRegister(MFRC522_REG_ERROR) & 0x1B)){
			status = MI_OK;
			if(n&irqEn&0x01){
				status = MI_NO_TAG;
			}
			if(cmd == PCD_TRANSCEIVE){
				n = readRegister(MFRC522_REG_FIFO_LEVEL);
				lastBits = readRegister(MFRC522_REG_CONTROL) & 0x07;
				if(lastBits){
					*rxLen = (n-1)*8 + lastBits;
				}
				else{
					*rxLen = n*8;
				}
				if(n == 0){
					n = 1;
				}
				if(n > MAXLEN){
					n = MAXLEN;
				}
				
				//read fifo buffer
				for(i = 0;i < n;i++){
					rxData[i] = readRegister(MFRC522_REG_FIFO_DATA);
				}
			}
		}
	}
	else{
		status = MI_ERROR;
	}
	setRegister(MFRC522_REG_CONTROL, 0x80);
	writeRsgister(MFRC522_REG_COMMAND, PCD_IDLE);
	return status;
}

RC522_Status RC522_Request(uint8_t reqMode, uint8_t* type){
	RC522_Status status;
	uint32_t rxDataLen;
	uint8_t cmdBuf[MAXLEN];
	
	clearRegister(MFRC522_REG_STATUS2, 0x08);
	writeRsgister(MFRC522_REG_BIT_FRAMING, 0x07);
	setRegister(MFRC522_REG_TX_CONTROL, 0x03);
	
	cmdBuf[0] = reqMode;
	status = CMDtoCard(PCD_TRANSCEIVE, cmdBuf, 1, cmdBuf, &rxDataLen);
	//printf("pcd request status %d\n", status);
	if((status == MI_OK)&&(rxDataLen == 0x10)){
		*type = cmdBuf[0];
		*(type +1) = cmdBuf[1];
	}
	else 
		status = MI_ERROR;
	return status;
}

RC522_Status anticoll(uint8_t* cardSerialNum){
	RC522_Status status;
	int i;
	uint8_t serialNumChk = 0;
	uint32_t unLen;
	uint8_t cmdBuf[MAXLEN];
	
	clearRegister(MFRC522_REG_STATUS2, 0x08);
	writeRsgister(MFRC522_REG_BIT_FRAMING, 0x00);
	clearRegister(MFRC522_REG_COLL, 0x80);
	
	cmdBuf[0] = PICC_ANTICOLL;
	cmdBuf[1] = 0x20;
	status = CMDtoCard(PCD_TRANSCEIVE, cmdBuf, 2, cmdBuf, &unLen);
	
	if(status == MI_OK){
		for(i=0;i<4;i++){
			*(cardSerialNum + i) = cmdBuf[i];
			serialNumChk ^= cmdBuf[i];
		}
		if(serialNumChk != cmdBuf[i]){
			status = MI_ERROR;
		}
	}
	setRegister(MFRC522_REG_COLL, 0x80);
	/*
	printf("card ");
	for(int i=0;i<4;i++){
		printf("%x ", cardSerialNum[i]);
	}
	printf("enter anticoll\r\n");
	*/
	return status;
}

//check has transfer error or not
void calculateCRC(uint8_t* txData, uint8_t len, uint8_t* rxData){
	uint8_t tmp;
	
	clearRegister(MFRC522_REG_DIV_IRQ, 0x04);
	writeRsgister(MFRC522_REG_COMMAND, PCD_IDLE);
	setRegister(MFRC522_REG_FIFO_LEVEL, 0x80);
	
	//write data into fifo buffer
	for(int i=0;i<len;i++){
		writeRsgister(MFRC522_REG_FIFO_DATA, *(txData+i));
	}
	
	writeRsgister(MFRC522_REG_COMMAND, PCD_CALCCRC);
	
	//wait CRC complete
	//uint32_t i = 2000000000;
	do{
		tmp = readRegister(MFRC522_REG_DIV_IRQ);
		//i--;
	}while(!(tmp&0x04));
	//while((i!=0)&&!(tmp&0x04));
	
	rxData[0] = readRegister(MFRC522_REG_CRC_RESULT_L);
	rxData[1] = readRegister(MFRC522_REG_CRC_RESULT_M);
//	printf("CRC result:");
//	for(int i =0;i<sizeof(rxData);i++){
//		printf("%x ", rxData[i]);
//	}
//	printf("\r\n");
}

void halt(){
	uint32_t unLen;
	uint8_t buf[MAXLEN];
	
	buf[0] = PICC_HALT;
	buf[1] = 0;
	
	calculateCRC(buf, 2, &buf[2]);
	CMDtoCard(PCD_TRANSCEIVE, buf, 4, buf, &unLen);
}

//select specific card 
uint8_t cardSelect(uint8_t* cardSerialNum){
	RC522_Status status;
	uint8_t buf[MAXLEN];
	uint32_t rxDataLen;
	
	buf[0] = PICC_SElECTTAG;
	buf[1] = 0x70;
	buf[6] = 0;
	
	//3,4,5,6 store uid
	for(uint8_t i=0;i<4;i++){
		buf[i +2] = *(cardSerialNum + i);
		buf[6] ^= *(cardSerialNum + i);
	}
	
	calculateCRC(buf, 7, &buf[7]);
	clearRegister(MFRC522_REG_STATUS2, 0x08);
	
	status = CMDtoCard(PCD_TRANSCEIVE, buf, 9, buf, &rxDataLen);
//	printf("select result: %d\r\n", status);
	if((status == MI_OK)&&(rxDataLen = 0x18))
		status = MI_OK;
	else{
		status = MI_ERROR;
	}
	
	return status;
}

//auth card pwd 
/*
authMode 
0x08 >> auth KEYA
0x61 >> auth KEYB
*/
uint8_t cardAuthPWD(uint8_t authMode, uint8_t blockAddr, uint8_t* cardKey, uint8_t* cardSerialNum){
	RC522_Status status;
	uint8_t buf[MAXLEN];
	uint32_t rxDataLen;
	
	buf[0] = authMode;
	buf[1] = blockAddr;
	
	for(int i=0;i<6;i++){
		buf[i+2] = *(cardKey + i);
	}
	for(int i=0;i<6;i++){
		buf[i+8] = *(cardSerialNum + i);
	}
	
	status = CMDtoCard(PCD_AUTHENT, buf, 12, buf, &rxDataLen);
	
	if((status != MI_OK)||(!(readRegister(MFRC522_REG_STATUS2) &0x08)))
		status = MI_ERROR;
	
	return status;
}

//read card specific block data
uint8_t readBlock(uint8_t blockAddr, uint8_t* rxData){
	RC522_Status status;
	uint8_t buf[MAXLEN];
	uint32_t rxDataLen;
	
	buf[0] = PICC_READ;
	buf[1] = blockAddr;
	calculateCRC(buf, 2, &buf[2]);
	status = CMDtoCard(PCD_TRANSCEIVE, buf, 4, buf, &rxDataLen);
	
	if((status == MI_OK)&&(rxDataLen == 0x90)){
		for(int i =0;i<16;i++)
			*(rxData + i) = buf[i];
	}
	else
		status = MI_ERROR;
	
	return status;
}

//write data into specific block
uint8_t writeBlock(uint8_t blockAddr, uint8_t* txData){
	printf("wait to write in ");
	
	for(int i=0;i<16;i++){
		printf("%x ", txData[i]);
	}
	printf("\r\n");
	RC522_Status status;
	uint8_t cmdBuf[MAXLEN];
	uint32_t ulLen;
	
	cmdBuf[0] = PICC_WRITE;
	cmdBuf[1] = blockAddr;
	
	calculateCRC(cmdBuf, 2, &cmdBuf[2]);
	
	status = CMDtoCard(PCD_TRANSCEIVE, cmdBuf, 4, cmdBuf, &ulLen);
//	printf("cmd status %d , ulLen %d\r\n", status, ulLen);
//	printf("cmdBuf %x\r\n", cmdBuf[0]);
	if((status != MI_OK) || (ulLen != 4) || ((cmdBuf[0]&0x0F) != 0x0A)){
		status = MI_ERROR;
		printf("write block fail at cmd to card \r\n");
	}
	
	if(status == MI_OK){
		memcpy(cmdBuf, txData, 16);
		for(int i =0;i<16;i++){
			cmdBuf[i] = *(txData + i);
		}
		calculateCRC(cmdBuf, 16, &cmdBuf[16]);
		//HAL_Delay(1);
		status = CMDtoCard(PCD_TRANSCEIVE, cmdBuf, 18, cmdBuf, &ulLen);
//		printf("cmd status %d , ulLen %d\r\n", status, ulLen);
//		printf("cmdBuf %x\r\n", cmdBuf[0]);
		if(status != MI_OK){
			printf("cmd error\r\n");
		}
		if((status != MI_OK) || (ulLen != 4) || ((cmdBuf[0]&0x0F) != 0x0A)){
			status = MI_ERROR;
			printf("write block fail\r\n");
		}
	}
	
	return status;
}
/*
cardSerialNum >> want modify
cardSerialNumScan >> find by rc522
*/
void writeCard(uint8_t* cardSerialNum, uint8_t keyType, uint8_t* key, uint8_t rw, uint8_t blockAddr, uint8_t* txData){
	RC522_Status status = 0;
	uint8_t cardSerialNumScan[4] = {0};
	//printf("%s\r\n", txData);
//	printf("rxData:");
//	for(int i=0;i<16;i++){
//		printf("%x ", txData[i]);
//	}
//	printf("\r\n");
	status = RC522_Request(PICC_REQALL, cardSerialNumScan);
	if(status == MI_OK){
			printf("find card:");
	}
	else{
		//printf("detect card fail, error code is %d\r\n", status);
		return;
	} 
		
	//anticoll
	status = anticoll(cardSerialNumScan);
	if(status != MI_OK)
		return;
	
	for(int i =0;i<4;i++){
		printf("%x ", cardSerialNumScan[i]);
	}
	printf("\r\n");
	
	status = cardSelect(cardSerialNumScan);
	if(status != MI_OK){
		printf("card not match, error code is %d\r\n", status);
		return;
	}
	
	if(keyType == 0)
		status = cardAuthPWD(KEYA, blockAddr, key, cardSerialNumScan);
	else
		status = cardAuthPWD(KEYB, blockAddr, key, cardSerialNumScan);
	
	if(status == MI_OK){
		printf("KEY Auth success\r\n");
	}
	else{
		printf("KEY not match\r\n");
		return;
	}
	
	/*
	rw
	1 >> read
	0 >> write
	*/
	if(rw == 1){
		status = readBlock(blockAddr, txData);
		if(status == MI_OK){
			printf("read data\r\n");
			for(int i =0;i<16;i++){
				printf("%x ", txData[i]);
			}
			printf("\r\n");
		}
		else{
			printf("readCardBlock() fail\r\n");
			return;
		}
	}
	else{
		status = writeBlock(blockAddr, txData);
		if(status == MI_OK)
			printf("wirte finish\r\n");
		else
			printf("write fail\r\n");
	}
	
	halt();
}