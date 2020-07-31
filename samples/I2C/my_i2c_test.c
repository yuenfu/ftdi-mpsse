/******************************************************************************/
/* 							 Include files									  */
/******************************************************************************/
/* Standard C libraries */
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
/* OS specific libraries */
#include<sys/time.h>

/* Include D2XX header*/
#include "ftd2xx.h"

/* Include libMPSSE header */
#include "libMPSSE_i2c.h"

/******************************************************************************/
/*							Macro and type defines							  */
/******************************************************************************/
/* Helper macros */
#define APP_CHECK_STATUS(exp) {if(exp!=FT_OK){printf("%s:%d:%s(): status(0x%x) \
!= FT_OK\n",__FILE__, __LINE__, __FUNCTION__,exp);exit(1);}else{;}};
#define CHECK_NULL(exp){if(exp==NULL){printf("%s:%d:%s():  NULL expression \
encountered \n",__FILE__, __LINE__, __FUNCTION__);exit(1);}else{;}};

/* Application specific macro definations */
#define I2C_DEVICE_ADDRESS 0x34
#define I2C_DEVICE_BUFFER_SIZE		(0x4)
#define START_ADDRESS_RAM 	0x00000
#define END_ADDRESS_RAM		0xD0000
#define RAM_TEST_BUFFER_SIZE	(END_ADDRESS_RAM-START_ADDRESS_RAM) //832kiB
#define CHANNEL_TO_OPEN			1	/*0 for first available channel, 1 for next... */
#define ADDR_STEP				4

/******************************************************************************/
/*							Global variables								  */
/******************************************************************************/
static FT_HANDLE ftHandle;
static uint8 buffer[I2C_DEVICE_BUFFER_SIZE] __attribute__((aligned(4))) = {0};
static uint8 ram_buffer[RAM_TEST_BUFFER_SIZE] __attribute__((aligned(4))) = {0};
static uint8 rand_buffer[RAM_TEST_BUFFER_SIZE] __attribute__((aligned(4))) = {0};
static uint32 reg_init[] = {
	0x005babe0 ,
	0x00400310 ,
	0x93496f0e ,
	0x008ffc33 ,
	0x00000000 ,
	0x96c0a331 ,
	0x04fb4000 ,
	0xfbbc9140 ,
	0x00002400 ,
	0x26646361 ,
	0x80000a91 ,
	0x11000000 ,
	0x00004920 ,
	0x72495a31 ,
	0x00703118 ,
	0x00910394 ,
	0x005e2a92 ,
	0x000723c3 ,
	0x0000110c ,
	0x2411badb ,
	0x026225db ,
	0x89458689 ,
	0x00000000 ,
	0x00516182 ,
	0x00010000 ,
	0x00000000 ,
	0x00000000 ,
	0x00000000 ,
	0x02c4a214 ,
	0x00003bc8 ,
};

/******************************************************************************/
/*						Public function definitions						 	  */
/******************************************************************************/
static FT_STATUS I2cReadReg(BYTE regAddr, BYTE* pData, ULONG ulLength)
{
	FT_STATUS ack = FALSE;
	BYTE Buffer[132];
	uint32 rl;

	Buffer[0] = regAddr;

	ack = I2C_DeviceWrite(ftHandle, I2C_DEVICE_ADDRESS, 1,
		Buffer, &rl,
		I2C_TRANSFER_OPTIONS_START_BIT|I2C_TRANSFER_OPTIONS_STOP_BIT
		|I2C_TRANSFER_OPTIONS_FAST_TRANSFER_BYTES);

	ack = I2C_DeviceRead(ftHandle, I2C_DEVICE_ADDRESS, 1,
		Buffer, &rl,
		I2C_TRANSFER_OPTIONS_START_BIT|I2C_TRANSFER_OPTIONS_STOP_BIT
		|I2C_TRANSFER_OPTIONS_FAST_TRANSFER_BYTES);

	memcpy(pData, Buffer, 1);

	return ack;
}
static FT_STATUS I2cWriteReg(BYTE regAddr, BYTE* pData, ULONG ulLength)
{
	FT_STATUS status;
	BYTE WriteBuffer[132];
	uint32 rl;

	WriteBuffer[0] = regAddr;
	memcpy(&WriteBuffer[1], pData, ulLength);

	status = I2C_DeviceWrite(ftHandle, I2C_DEVICE_ADDRESS, ulLength + 1,
		WriteBuffer, &rl,
		I2C_TRANSFER_OPTIONS_START_BIT|I2C_TRANSFER_OPTIONS_STOP_BIT
		|I2C_TRANSFER_OPTIONS_FAST_TRANSFER_BYTES);

	APP_CHECK_STATUS(status);
	return status;
}
static FT_STATUS read_word(uint32 addr, uint32 *data)
{
	BYTE ucAddr = 0, ucData = 0, ucCmd = 0, ucStatus = 0;
	UINT ulCnt = 0, ulData = 0;
	FT_STATUS status = FT_OK;

	//1. Write the AHB address (20 24 bits) of register you want to read to I2C register {Reg6[3:0], Reg5, Reg0}.
	ucAddr = (BYTE)((addr >> 16) & 0x0F);	//0x0C;
	I2cWriteReg(0x06, &ucAddr, 1);
	ucAddr = (BYTE)((addr >> 8) & 0xFF);
	I2cWriteReg(0x05, &ucAddr, 1);
	ucAddr = (BYTE)(addr & 0xFF);
	I2cWriteReg(0x04, &ucAddr, 1);

	//2. Write 0x0D to I2C Reg7, to prepare the AHB bus read command.
	ucCmd = 0x0D;
	I2cWriteReg(0x07, &ucCmd, 1);

	//3. Read I2C Reg7[4] to check whether the read command finished.
	//	 Reg7[4] = 1-- the read command is still pending.
	//	 Reg7[4] = 0 - the read command is finished.
	do
	{
		if(ulCnt >= 100)
		{
			return FALSE;
		}
		I2cReadReg(0x07, &ucStatus, 1);
		ulCnt ++;
	} while ((ucStatus & 0x10) == 0x10);

	//4. After Reg7[4] is read 1's, the read data has been kept in I2C register {Reg4, Reg3, Reg2, Reg1}.
	//5. Read I2C register  Reg4, Reg3, Reg2, and Reg1 to get the 32-bits register data.
	I2cReadReg(0x03, &ucData, 1);
	ulData = ucData;
	I2cReadReg(0x02, &ucData, 1);
	ulData <<= 8;
	ulData += ucData;
	I2cReadReg(0x01, &ucData, 1);
	ulData <<= 8;
	ulData += ucData;
	I2cReadReg(0x00, &ucData, 1);
	ulData <<= 8;
	ulData += ucData;

	*data = ulData;

	printf("data=%08x\n", *data);

	APP_CHECK_STATUS(status);
rc:
	return status;
}
static FT_STATUS write_word(uint32 addr, uint32 data)
{
	BYTE ucAddr = 0, ucData = 0, ucCmd = 0, ucStatus = 0;
	UINT ulCnt = 0;
	FT_STATUS status;

	//1. Write the AHB address (20 24 bits) of register you want to write to I2C register {Reg6[3:0], Reg5, Reg0}.
	ucAddr = (BYTE)((addr >> 16) & 0x0F);	//0x0C;
	I2cWriteReg(0x06, &ucAddr, 1);
	ucAddr = (BYTE)((addr >> 8) & 0xFF);
	I2cWriteReg(0x05, &ucAddr, 1);
	ucAddr = (BYTE)(addr & 0xFF);
	I2cWriteReg(0x04, &ucAddr, 1);

	//2. Write data (32-bits) to I2C register {Reg4, Reg3, Reg2, Reg1}.
	ucData = (BYTE)((data >> 24) & 0xFF);
	I2cWriteReg(0x03, &ucData, 1);
	ucData = (BYTE)((data >> 16) & 0xFF);
	I2cWriteReg(0x02, &ucData, 1);
	ucData = (BYTE)((data >> 8) & 0xFF);
	I2cWriteReg(0x01, &ucData, 1);
	ucData = (BYTE)(data & 0xFF);
	I2cWriteReg(0x00, &ucData, 1);

	//3. Write 0x0B to I2C Reg7, to prepare the AHB bus write command
	ucCmd = 0x0B;
	I2cWriteReg(0x07, &ucCmd, 1);

	//4.Read I2C Reg7[4] to check whether the write command finished, if necessary.
	//	Reg7[4] = 1-- the write command is still pending.
	//	Reg7[4] = 0 - the write command is finished.
	do
	{
		if(ulCnt >= 100)
		{
			return FALSE;
		}
		I2cReadReg(0x07, &ucStatus, 1);
		ulCnt ++;
	} while ((ucStatus & 0x10) == 0x10);


	APP_CHECK_STATUS(status);
	return status;
}
#define SRAM_INITIAL_START 0x10000
#define SRAM_ADDR_IN_BACK(addr)  (addr & 0xFFFF)
#define SRAM_VALUE(value)  (value & 0xFFFFF)
#define SRAM_LAST_ADDR     0xFFFC
#define SRAM_BANK(addr) ((addr & 0xF0000) >> 16)
#define SRAM_BANK_COUNT 13
#define swap32(x)							\
	(uint32)(((uint32)(x) & 0xff) << 24 |				\
	((uint32)(x) & 0xff00) << 8 | ((uint32)(x) & 0xff0000) >> 8 |	\
	((uint32)(x) & 0xff000000) >> 24)
static uint32 sram_initial_value(uint32 addr)
{
	return SRAM_INITIAL_START | (SRAM_ADDR_IN_BACK(addr)>>2);
}
int main(void)
{
	FT_STATUS status = FT_OK;
#ifdef CONFIG_SHOW_CHANNEL_INFO
	FT_DEVICE_LIST_INFO_NODE devList = {0};
#endif
	ChannelConfig channelConf = {0};
	uint32 address = 0;
	uint32 channels = 0;
	uint32 data = 0;
	uint32 *p;
	uint32 *q;
	uint32 i, j;
	uint8 latency = 1;	//milliseconds, USB handle freq.
	struct timeval ta, tb, tres;
	
	channelConf.ClockRate = I2C_CLOCK_STANDARD_MODE;/*i.e. 100 KHz*/
	channelConf.LatencyTimer = latency;
	//channelConf.Options = I2C_DISABLE_3PHASE_CLOCKING;
	channelConf.Options = I2C_ENABLE_DRIVE_ONLY_ZERO;

	/* init library */
	status = I2C_GetNumChannels(&channels);
	APP_CHECK_STATUS(status);
	if (FT_OK != status)
		goto fail;
	//printf("Number of available I2C channels = %d\n",(int)channels);

	if (channels>0)
	{
		printf("[I2C] buffer:%p len:%08lx\n", buffer, sizeof(buffer));
		printf("[I2C] ram_buffer:%p len:%08lx\n", ram_buffer, sizeof(ram_buffer));
		printf("[I2C] rand_buffer:%p len:%08lx\n", rand_buffer, sizeof(rand_buffer));
		printf("[I2C] set clock = %d Hz\n", channelConf.ClockRate);

#ifdef CONFIG_SHOW_CHANNEL_INFO
		for (i=0;i<channels;i++)
		{
			status = I2C_GetChannelInfo(i,&devList);
			APP_CHECK_STATUS(status);
			if (FT_OK != status)
				goto fail;
			printf("Information on channel number %d:\n",i);
			/* print the dev info */
			printf("		Flags=0x%x\n",devList.Flags);
			printf("		Type=0x%x\n",devList.Type);
			printf("		ID=0x%x\n",devList.ID);
			printf("		LocId=0x%x\n",devList.LocId);
			printf("		SerialNumber=%s\n",devList.SerialNumber);
			printf("		Description=%s\n",devList.Description);
			printf("		ftHandle=%p\n",devList.ftHandle);/*is 0 unless open*/
		}
#endif

		/* Open the first available channel */
		status = I2C_OpenChannel(CHANNEL_TO_OPEN,&ftHandle);
		APP_CHECK_STATUS(status);
		if (FT_OK != status)
			goto fail;
		printf("[I2C] open channel %c\n", CHANNEL_TO_OPEN ? 'B' : 'A');
		//printf("\nhandle=%p status=0x%x\n",ftHandle,status);
		status = I2C_InitChannel(ftHandle,&channelConf);
		APP_CHECK_STATUS(status);
		if (FT_OK != status)
			goto fail;
		printf("[I2C] init channel %c\n", CHANNEL_TO_OPEN ? 'B' : 'A');

#if 0
		data = 0x0e0e0e0e;
		I2cWriteReg(0x6, (char *)&data, 1);
#endif
#if 0
		I2cReadReg(0x7, (char *)&data, 1);
		printf("data = %08x\n", data);
#endif
#if 1
		address = 0xe0b00;
		read_word(address, &data);
#endif
#if 0
		/* register test */
		printf("[I2C] Register access test: ");
		gettimeofday(&ta, NULL);
		//initial value check
		address = 0xe0b00;
		for (i=0; i<30*ADDR_STEP; i+=ADDR_STEP)
		{
			read_word(address+i,(uint32 *)ram_buffer);
			p = (uint32 *)ram_buffer;
			if (swap32(*p) != reg_init[(i/ADDR_STEP)]) {
				printf("register value Fail: [0x%06X] %08X != %08X\n", address+i, swap32(*p), reg_init[(i/ADDR_STEP)]);
				goto fail;
			}
		}
		gettimeofday(&tb, NULL);
		timersub(&tb, &ta, &tres);
		printf("Pass (%ld.%06ld seconds)\n", tres.tv_sec, tres.tv_usec);

		/* RAM test */
		//initial value check
		for (i=0, p = (uint32 *)ram_buffer; i<RAM_TEST_BUFFER_SIZE; i+=ADDR_STEP, p++)
		{
			read_word(address+i, p);
			if (swap32(*p) != sram_initial_value(i)) {
				printf("RAM value Fail: [0x%06X] %08X != %08X\n", i, swap32(*p), sram_initial_value(i));
				goto fail;
			}
		}

		//random data, write, read back, check
		printf("[I2C] RAM access test: ");
		gettimeofday(&ta, NULL);
		for (j=0; j<2; j++)
		{
			srand(41);
			for (i=0, p = (uint32 *)rand_buffer; i<RAM_TEST_BUFFER_SIZE; i+=ADDR_STEP, p++)
			{
				*p = swap32(SRAM_VALUE(rand()));
				//printf("[%d] %p=0x%08x\n", i, p, *p);
			}
			memcpy(ram_buffer, rand_buffer, RAM_TEST_BUFFER_SIZE); //rand_buffer -> ram_buffer
			for (address=START_ADDRESS_RAM;address<END_ADDRESS_RAM;address+=ADDR_STEP)
			{
				write_word(address,ram_buffer[address]);
			}
			for (address=START_ADDRESS_RAM;address<END_ADDRESS_RAM;address+=ADDR_STEP)
			{
				read_word(address,(uint32 *)&ram_buffer[address]);
			}
			for (i=0, p = (uint32 *)ram_buffer, q = (uint32 *)rand_buffer; i<RAM_TEST_BUFFER_SIZE; i+=ADDR_STEP, p++, q++)
			{
				if (*p != *q) {
					printf("RAM value Fail: [0x%06X] %08X != %08X\n", i, swap32(*p), swap32(*q));
					goto fail;
				}
			}
		}
		gettimeofday(&tb, NULL);
		timersub(&tb, &ta, &tres);
		printf("Pass (%ld.%06ld seconds)\n", tres.tv_sec, tres.tv_usec);

		//initial value restore
		for (i=0, p = (uint32 *)ram_buffer; i<RAM_TEST_BUFFER_SIZE; i+=ADDR_STEP, p++)
		{
			*p = swap32(sram_initial_value(i));
		}
		for (address=START_ADDRESS_RAM;address<END_ADDRESS_RAM;address+=ADDR_STEP)
		{
			write_word(address,ram_buffer[address]);
		}
#endif

		status = I2C_CloseChannel(ftHandle);
	}
	else {
		printf("no available I2C channels!!!\n");
		goto fail;
	}

	return 0;
fail:
	printf("failed\n");
	return 1;
}

