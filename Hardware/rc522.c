#include "rc522.h"
#include "stdio.h"
#include "spi.h"
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
	
	AntennaON();
	
	printf("init sucess\t\n");
}


RC522_Status getSerialNum(uint8_t* cardSerialNum){
	RC522_Status status;
	status = RC522_Request(PICC_REQIDL, cardSerialNum);
	if(status == MI_OK){
		status = anticoll(cardSerialNum);
		halt();
		return status;
	}
	else{
		return status;
	}
}

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
	//HAL_SPI_Transmit(&hspi2, &regAddr, 1, 500);
	//HAL_SPI_Transmit(&hspi2, &cmd, 1, 500);
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
	//HAL_SPI_Transmit(&hspi2, &regAddr, 1, 500);
	//HAL_SPI_Receive(&hspi2, &result, 1, 500);
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
	
	i = 2000000;
	do{
		n = readRegister(MFRC522_REG_COMM_IRQ);
		i--;
	}while((i!=0) && !(n&0x01) && !(n&waitIRq));
	
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
	//printf("cmd to card status %d\n", status);
	return status;
}

RC522_Status RC522_Request(uint8_t reqMode, uint8_t* type){
	RC522_Status status;
	uint32_t rxData;
	
	writeRsgister(MFRC522_REG_BIT_FRAMING, 0x07);
	
	type[0] = reqMode;
	status = CMDtoCard(PCD_TRANSCEIVE, type, 1, type, &rxData);
	//printf("pcd request status %d\n", status);
	if((status != MI_OK)||(rxData != 0x10)){
		status = MI_ERROR;
	}
	return status;
}

RC522_Status anticoll(uint8_t* cardSerialNum){
	RC522_Status status;
	int i;
	uint8_t serialNumChk = 0;
	uint32_t unLen;
	
	writeRsgister(MFRC522_REG_BIT_FRAMING, 0x00);
	
	cardSerialNum[0] = PICC_ANTICOLL;
	cardSerialNum[1] = 0x20;
	status = CMDtoCard(PCD_TRANSCEIVE, cardSerialNum, 2, cardSerialNum, &unLen);
	
	if(status == MI_OK){
		for(i=0;i<4;i++){
			serialNumChk ^= cardSerialNum[i];
		}
		if(serialNumChk != cardSerialNum[i]){
			status = MI_ERROR;
		}
	}
	printf("anticoll status %d\t\n", status);
	return status;
}

void calculateCRC(uint8_t* txData, uint8_t len, uint8_t* rxData){
	uint8_t tmp;
	clearRegister(MFRC522_REG_DIV_IRQ, 0x04);
	setRegister(MFRC522_REG_FIFO_LEVEL, 0x80);
	
	//write data into fifo buffer
	for(int i=0;i<len;i++){
		writeRsgister(MFRC522_REG_FIFO_DATA, *(txData+i));
	}
	
	writeRsgister(MFRC522_REG_COMMAND, PCD_CALCCRC);
	
	//wait CRC complete
	uint8_t i = 0xFF;
	do{
		tmp = readRegister(MFRC522_REG_DIV_IRQ);
		i--;
	}while((i!=0) && !(tmp&0x04));
	
	rxData[0] = readRegister(MFRC522_REG_CRC_RESULT_L);
	rxData[1] = readRegister(MFRC522_REG_CRC_RESULT_M);
}

void halt(){
	uint32_t unLen;
	uint8_t buf[4];
	
	buf[0] = PICC_HALT;
	buf[1] = 0;
	
	calculateCRC(buf, 2, &buf[2]);
	CMDtoCard(PCD_TRANSCEIVE, buf, 4, buf, &unLen);
}