#include <atmel_start.h>

typedef struct TXBUFFER TX_BUFFER_DESC_t;
typedef struct RXBUFFER RX_BUFFER_DESC_t;

struct TXBUFFER {
		bool initDone;
		
		uint16_t size;
		uint8_t * buf;
	
		// indices
		uint16_t wrIdx;
		uint16_t rdIdx;
		
		// needed to automatically restart transmission
		const struct usart_async_descriptor * io_descr;
		
		//
		uint8_t transmissionActive;
			
		// functions
		void (*onAsyncUartTxFinishedIsr)( TX_BUFFER_DESC_t * pBuffer );
		uint16_t (*writeTxBuffer)( TX_BUFFER_DESC_t * pBuffer, uint8_t * pBuf, uint16_t len );
		
	};

TX_BUFFER_DESC_t txBuffer;
	


struct RXBUFFER {
		
		uint16_t size;
		uint8_t * buf;

		// indices
		uint16_t wrIdx;
		uint16_t rdIdx;

		// functions
		void (*onAsyncUartRxCharIsr)( RX_BUFFER_DESC_t * pBuffer, uint8_t rxData );
		uint16_t (*readRxBuffer)( RX_BUFFER_DESC_t * pBuffer, uint8_t * pBuf, uint16_t maxLen);
		
	} ;

RX_BUFFER_DESC_t rxBuffer;

// prototypes	

// RX Buffer
static void rxBufferInit(RX_BUFFER_DESC_t * pBuffer, uint8_t * pBuf, uint16_t bufferSize );
static void onAsyncUartRxCharIsr( RX_BUFFER_DESC_t* pBuffer, uint8_t rxData);
static uint16_t readRxBuffer(RX_BUFFER_DESC_t * pBuffer, uint8_t * pBuf, uint16_t maxLen );

// TX Buffer
static void txBufferInit(TX_BUFFER_DESC_t * pBuffer, const struct usart_async_descriptor *const io_descr, uint8_t * pBuf, uint16_t bufferSize );
static void onAsyncUartTxFinishedIsr( TX_BUFFER_DESC_t* pBuffer );
static void txRetriggerTransmission( TX_BUFFER_DESC_t* pBuffer );
static uint16_t writeTxBuffer(TX_BUFFER_DESC_t * pBuffer, uint8_t * pBuf, uint16_t len );


static void tx_cb_USART_0(const struct usart_async_descriptor *const io_descr)
{
	if (txBuffer.initDone == 0){
		return;
	}
	
	/* Transfer completed */
	txBuffer.onAsyncUartTxFinishedIsr( &txBuffer );
}

static void rx_cb_USART_0(const struct usart_async_descriptor *const io_descr)
{
	/* Transfer completed */
	uint8_t rxData;
	
	
	// read from (small-) ringbuffer
	io_read(io_descr, &rxData, 1);

	// write to large ringbuffer
	rxBuffer.onAsyncUartRxCharIsr(&rxBuffer, rxData);
	
}



int main(void)
{
	/* Initializes MCU, drivers and middleware */
	atmel_start_init();

	struct io_descriptor *io;
	
	usart_async_register_callback(&USART_0, USART_ASYNC_TXC_CB, tx_cb_USART_0);
	usart_async_register_callback(&USART_0, USART_ASYNC_RXC_CB, rx_cb_USART_0);
	/*usart_async_register_callback(&USART_0, USART_ASYNC_ERROR_CB, err_cb);*/
	usart_async_get_io_descriptor(&USART_0, &io);
	usart_async_enable(&USART_0);
	

	
	uint8_t someBuffer[2000];
	rxBufferInit(&rxBuffer, someBuffer, 2000);
	
	uint8_t someOtherBuffer[3000];
	txBufferInit(&txBuffer, io, someOtherBuffer, 3000);

	
	#define MAX_BUF_COUNT 10
	uint8_t buf[MAX_BUF_COUNT];
		
	/* Replace with your application code */
	while (1) {
		
		
		uint16_t rxCount= rxBuffer.readRxBuffer( &rxBuffer, buf, MAX_BUF_COUNT );
		
		if (rxCount > 0){
			// we received something
			writeTxBuffer(&txBuffer, buf, rxCount);
		}
		
	}
}

void rxBufferInit(RX_BUFFER_DESC_t * pBuffer, uint8_t * pBuf, uint16_t bufferSize ){

	pBuffer->size=bufferSize;
	pBuffer->buf= pBuf;
	
	pBuffer->wrIdx= 0;
	pBuffer->rdIdx= 0;
	
	pBuffer->onAsyncUartRxCharIsr= onAsyncUartRxCharIsr;	
	pBuffer->readRxBuffer= readRxBuffer;	
}

/*
	this routine runs in ISR context
*/
void onAsyncUartRxCharIsr( RX_BUFFER_DESC_t* pBuffer, uint8_t rxData){
	pBuffer->buf[pBuffer->wrIdx++]= rxData;
	if (pBuffer->wrIdx >= pBuffer->size){
		pBuffer->wrIdx= 0;
	}
}

/*
	this routine runs in user context 
*/
uint16_t readRxBuffer(RX_BUFFER_DESC_t * pBuffer, uint8_t * pBuf, uint16_t maxLen ){
	
	uint16_t rxBufferWrIdx_copy;

	CRITICAL_SECTION_ENTER()
	rxBufferWrIdx_copy= pBuffer->wrIdx;
	CRITICAL_SECTION_LEAVE()
	
	if (pBuffer->rdIdx == rxBufferWrIdx_copy){
		// nothing to do
		return 0;
	}
	
	uint16_t count= 0;
	
	// copy
	while( pBuffer->rdIdx != rxBufferWrIdx_copy ){
		pBuf[count++]= pBuffer->buf[pBuffer->rdIdx++];
		// wrap
		if (pBuffer->rdIdx >= pBuffer->size){
			pBuffer->rdIdx= 0;
		}
		// max buf size
		if (count >= maxLen){
			break;
		}
	}
		
	return count;
}

// 
//
void txBufferInit( TX_BUFFER_DESC_t * pBuffer, const struct usart_async_descriptor *const io_descr, uint8_t * pBuf, uint16_t bufferSize ){
	
	pBuffer->size=bufferSize;
	pBuffer->buf= pBuf;
		
	pBuffer->wrIdx= 0;
	pBuffer->rdIdx= 0;

	pBuffer->io_descr= io_descr;
				
	pBuffer->onAsyncUartTxFinishedIsr= onAsyncUartTxFinishedIsr;
	pBuffer->writeTxBuffer= writeTxBuffer;
	
	pBuffer->initDone= 1;		
}

void onAsyncUartTxFinishedIsr( TX_BUFFER_DESC_t* pBuffer ){
	
	// reset active transmission
	pBuffer->transmissionActive= 0;
	
	// check if there is something to do
	if (pBuffer->wrIdx == pBuffer->rdIdx){
		return;
	}
	
	//
	txRetriggerTransmission(pBuffer);
		
}

 uint16_t writeTxBuffer(TX_BUFFER_DESC_t * pBuffer, uint8_t * pBuf, uint16_t len ){
	 
	// add to tx buffer
	uint16_t wrIdx_new= pBuffer->wrIdx;
	for (uint16_t i=0; i< len; i++){
		pBuffer->buf[wrIdx_new++]= pBuf[i];
		// wrap around
		if (wrIdx_new >= pBuffer->size){
			wrIdx_new= 0;
		}
	}

	uint8_t isActive= 0;
		
	// finally set new idx
	CRITICAL_SECTION_ENTER()
	pBuffer->wrIdx= wrIdx_new;
	isActive= pBuffer->transmissionActive;
	CRITICAL_SECTION_LEAVE()
		
	if (isActive == 0){
		txRetriggerTransmission( pBuffer );
	}
		
	return 1;
}

/*
	!! ATTENTION !! 
	this routine can be called from ISR context or user context
	- if called from user context, make sure that "transmissionActive == 0" before calling it!
	- for effectiveness this check is done outter here; as it is not needed when called from ISR
*/
void txRetriggerTransmission( TX_BUFFER_DESC_t* pBuffer ){
	
	// set to active
	txBuffer.transmissionActive= 1;
	
	uint16_t count= 0;
	
	// calc correct transmission count
	if (pBuffer->wrIdx > pBuffer->rdIdx){
		count= pBuffer->wrIdx - pBuffer->rdIdx;
		} else {
		count= pBuffer->size - pBuffer->rdIdx;
	}
	
	// re-calc idx
	uint16_t oldRdIdx= pBuffer->rdIdx;
	pBuffer->rdIdx += count;
	
	// limit / wrap
	if (pBuffer->rdIdx >= pBuffer->size){
		pBuffer->rdIdx= 0;
	}
	
	// restart writing
	io_write(pBuffer->io_descr, &(pBuffer->buf[oldRdIdx]), count);
	
}
