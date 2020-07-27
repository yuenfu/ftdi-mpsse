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
#include "libMPSSE_spi.h"

/******************************************************************************/
/*							Macro and type defines							  */
/******************************************************************************/
/* Helper macros */
#define APP_CHECK_STATUS(exp) {if(exp!=FT_OK){printf("%s:%d:%s(): status(0x%x) \
!= FT_OK\n",__FILE__, __LINE__, __FUNCTION__,exp);exit(1);}else{;}};
#define CHECK_NULL(exp){if(exp==NULL){printf("%s:%d:%s():  NULL expression \
encountered \n",__FILE__, __LINE__, __FUNCTION__);exit(1);}else{;}};

/* Application specific macro definations */
#define SPI_CLK_RATE 6000000 //Hz
#define SPI_DEVICE_BUFFER_SIZE		(0x1000+3) //4kiB + 3B(command + address)
#define SPI_WRITE_COMPLETION_RETRY		10
#define START_ADDRESS_RAM 	0x00000
#define END_ADDRESS_RAM		0xD0000
#define RAM_TEST_BUFFER_SIZE	(END_ADDRESS_RAM-START_ADDRESS_RAM) //832kiB
#define CHANNEL_TO_OPEN			0	/*0 for first available channel, 1 for next... */
#define ADDR_STEP				4

/******************************************************************************/
/*							Global variables								  */
/******************************************************************************/
static FT_HANDLE ftHandle;
static uint8 buffer[SPI_DEVICE_BUFFER_SIZE] __attribute__((aligned(4))) = {0};
static uint8 ram_buffer[RAM_TEST_BUFFER_SIZE] __attribute__((aligned(4))) = {0};
static uint8 rand_buffer[RAM_TEST_BUFFER_SIZE] __attribute__((aligned(4))) = {0};

/******************************************************************************/
/*						Public function definitions						 	  */
/******************************************************************************/
static FT_STATUS read_word(uint32 address, uint32 *data)
{
	uint32 sizeToTransfer = 0;
	uint32 sizeTransfered = 0;
	uint8 writeComplete=0;
	uint32 retry=0;
	FT_STATUS status;

	/* Write command + 4K offset */
	sizeToTransfer=3;
	sizeTransfered=0;
	buffer[0] = 0x00; //read command
	buffer[0] |= ((address >> 6) & 0x3F);
	buffer[1] = (address << 2) & 0xFF;
	//printf("address=%08x\n", address);
	//printf("%02x %02x %02x\n", buffer[0], buffer[1], buffer[2]);
	status = SPI_Write(ftHandle, buffer, sizeToTransfer, &sizeTransfered,
		SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE);
	APP_CHECK_STATUS(status);
	if (FT_OK != status)
		goto rc;

	/*Read 4 bytes*/
	sizeToTransfer=4;
	sizeTransfered=0;
	status = SPI_Read(ftHandle, buffer, sizeToTransfer, &sizeTransfered,
		SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_DISABLE);

	*data = *(uint32 *)buffer;
	printf("data=%08x\n", *data);
	printf("%02x %02x %02x %02x\n", buffer[0], buffer[1], buffer[2], buffer[3]);

	APP_CHECK_STATUS(status);
rc:
	return status;
}
static FT_STATUS read_multi_word(uint32 address, uint8 *data, uint32 length)
{
	uint32 sizeToTransfer = 0;
	uint32 sizeTransfered = 0;
	uint8 writeComplete=0;
	uint32 retry=0;
	FT_STATUS status;

	if (length > 0x1000) {
		status = FT_INVALID_PARAMETER;
		goto rc;
	}

	/* Write command + 4K offset */
	sizeToTransfer=3;
	sizeTransfered=0;
	buffer[0] = 0x40; //read command
	buffer[0] |= ((address >> 6) & 0x3F);
	buffer[1] = (address << 2) & 0xFF;
	//printf("address=%08x\n", address);
	//printf("%02x %02x %02x\n", buffer[0], buffer[1], buffer[2]);
	status = SPI_Write(ftHandle, buffer, sizeToTransfer, &sizeTransfered,
		SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE);
	APP_CHECK_STATUS(status);
	if (FT_OK != status)
		goto rc;

	/*Read 4 bytes*/
	sizeToTransfer=length;
	sizeTransfered=0;
	status = SPI_Read(ftHandle, buffer, sizeToTransfer, &sizeTransfered,
		SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_DISABLE);

	memcpy(data, buffer, length);
	//printf("data=%08x\n", *data);
	//printf("%02x %02x %02x %02x\n", buffer[0], buffer[1], buffer[2], buffer[3]);

rc:
	APP_CHECK_STATUS(status);
	return status;
}
static FT_STATUS write_base(uint32 base)
{
	uint32 sizeToTransfer = 7; //24 + 32 bit = 7 bytes
	uint32 sizeTransfered = 0;
	uint8 writeComplete=0;
	uint32 retry=0;
	FT_STATUS status;

	buffer[0] = 0x80; //write base command
	buffer[3] = (base >> 24) & 0xFF;
	buffer[4] = (base >> 16) & 0xFF;
	buffer[5] = (base >> 8) & 0xFF;
	buffer[6] = (base >> 0) & 0xFF;
	//printf("base=%08x\n", base);
	//printf("%02x %02x %02x %02x %02x %02x %02x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6]);
	status = SPI_Write(ftHandle, buffer, sizeToTransfer, &sizeTransfered,
		SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_DISABLE);

	APP_CHECK_STATUS(status);
	return status;
}
static FT_STATUS write_word(uint32 address, uint32 data)
{
	uint32 sizeToTransfer = 3+4; //24 + 32 bit = 7 bytes
	uint32 sizeTransfered = 0;
	uint8 writeComplete=0;
	uint32 retry=0;
	FT_STATUS status;

	buffer[0] = 0xc0; //write command
	buffer[0] |= (address >> 16) & 0xFF;
	buffer[1] = (address >> 8) & 0xFF;
	buffer[2] = (address >> 0) & 0xFF;
	buffer[3] = (data >> 24) & 0xFF;
	buffer[4] = (data >> 16) & 0xFF;
	buffer[5] = (data >> 8) & 0xFF;
	buffer[6] = (data >> 0) & 0xFF;
	//printf("address=%08x data=%08x\n", address, data);
	//printf("%02x %02x %02x %02x %02x %02x %02x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6]);
	status = SPI_Write(ftHandle, buffer, sizeToTransfer, &sizeTransfered,
		SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_DISABLE);

	APP_CHECK_STATUS(status);
	return status;
}
static FT_STATUS write_multi_word(uint32 address, uint8 *data, uint32 length)
{
	uint32 sizeToTransfer = 3+length; //24 + 32 bit = 7 bytes
	uint32 sizeTransfered = 0;
	uint8 writeComplete=0;
	uint32 retry=0;
	FT_STATUS status;

	if (length > 0x1000) {
		status = FT_INVALID_PARAMETER;
		goto rc;
	}

	buffer[0] = 0xc0; //write command
	buffer[0] |= (address >> 16) & 0xFF;
	buffer[1] = (address >> 8) & 0xFF;
	buffer[2] = (address >> 0) & 0xFF;
	memcpy(&buffer[3], data, length);
	//printf("address=%08x data=%08x\n", address, data);
	//printf("%02x %02x %02x %02x %02x %02x %02x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6]);
	status = SPI_Write(ftHandle, buffer, sizeToTransfer, &sizeTransfered,
		SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE|
		SPI_TRANSFER_OPTIONS_CHIPSELECT_DISABLE);

rc:
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
	uint8 latency = 2;	//milliseconds, USB handle freq.
	struct timeval ta, tb, tres;
	
	channelConf.ClockRate = SPI_CLK_RATE;
	channelConf.LatencyTimer = latency;
	channelConf.configOptions = SPI_CONFIG_OPTION_MODE0 | SPI_CONFIG_OPTION_CS_DBUS3 | SPI_CONFIG_OPTION_CS_ACTIVELOW;
	channelConf.Pin = 0x00000000;/*FinalVal-FinalDir-InitVal-InitDir (for dir 0=in, 1=out)*/

	/* init library */
	status = SPI_GetNumChannels(&channels);
	APP_CHECK_STATUS(status);
	if (FT_OK != status)
		goto fail;
	//printf("Number of available SPI channels = %d\n",(int)channels);

	if (channels>0)
	{
		printf("[SPI] buffer:%p len:%08lx\n", buffer, sizeof(buffer));
		printf("[SPI] ram_buffer:%p len:%08lx\n", ram_buffer, sizeof(ram_buffer));
		printf("[SPI] rand_buffer:%p len:%08lx\n", rand_buffer, sizeof(rand_buffer));
		printf("[SPI] set clock = %d Hz\n", SPI_CLK_RATE);

#ifdef CONFIG_SHOW_CHANNEL_INFO
		for (i=0;i<channels;i++)
		{
			status = SPI_GetChannelInfo(i,&devList);
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
		status = SPI_OpenChannel(CHANNEL_TO_OPEN,&ftHandle);
		APP_CHECK_STATUS(status);
		if (FT_OK != status)
			goto fail;
		printf("[SPI] open channel %c\n", CHANNEL_TO_OPEN ? 'B' : 'A');
		//printf("\nhandle=%p status=0x%x\n",ftHandle,status);
		status = SPI_InitChannel(ftHandle,&channelConf);
		APP_CHECK_STATUS(status);
		if (FT_OK != status)
			goto fail;
		printf("[SPI] init channel %c\n", CHANNEL_TO_OPEN ? 'B' : 'A');

		/* register test */
		printf("[SPI] Register access test: ");
		gettimeofday(&ta, NULL);
		address = 0xe0b00;
		write_base(address);
		read_multi_word(address,ram_buffer,0x10);
		//for (i=0; i<0x10; i+=ADDR_STEP)
		//{
		//	printf("%02x %02x %02x %02x\n", ram_buffer[i], ram_buffer[i+1], ram_buffer[i+2], ram_buffer[i+3]);
		//}
		gettimeofday(&tb, NULL);
		timersub(&tb, &ta, &tres);
		printf("Pass (%ld.%06ld seconds)\n", tres.tv_sec, tres.tv_usec);

		/* RAM test */
		//initial value check
		for (address=START_ADDRESS_RAM;address<END_ADDRESS_RAM;address+=0x1000)
		{
			write_base(address);
			read_multi_word(address,ram_buffer+address,0x1000);
		}
		for (i=0, p = (uint32 *)ram_buffer; i<RAM_TEST_BUFFER_SIZE; i+=ADDR_STEP, p++)
		{
			if (swap32(*p) != sram_initial_value(i)) {
				printf("RAM value Fail: [0x%06X] %08X != %08X\n", i, swap32(*p), sram_initial_value(i));
				goto fail;
			}
		}

		//random data, write, read back, check
		printf("[SPI] RAM access test: ");
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
			for (address=START_ADDRESS_RAM;address<END_ADDRESS_RAM;address+=0x1000)
			{
				write_base(address);
				write_multi_word(address,ram_buffer+address,0x1000);
			}
			for (address=START_ADDRESS_RAM;address<END_ADDRESS_RAM;address+=0x1000)
			{
				write_base(address);
				read_multi_word(address,ram_buffer+address,0x1000);
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
		for (address=START_ADDRESS_RAM;address<END_ADDRESS_RAM;address+=0x1000)
		{
			write_base(address);
			write_multi_word(address,ram_buffer+address,0x1000);
		}

		status = SPI_CloseChannel(ftHandle);
	}
	else {
		printf("no available SPI channels!!!\n");
		goto fail;
	}

	return 0;
fail:
	printf("failed\n");
	return 1;
}

