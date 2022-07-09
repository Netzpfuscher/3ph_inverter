/*
 * m365
 *
 * Copyright (c) 2021 Jens Kerrinnes
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "main.h"
#include "task_cli.h"
#include <string.h>
#include "cmsis_os.h"
#include "TTerm.h"
#include <stdio.h>
#include "usart.h"
#include "init.h"
#include "stdarg.h"
#include "ff.h"
#include "fatfs.h"

void putbuffer(unsigned char *buf, unsigned int len, port_str * port){
	xSemaphoreTake(port->tx_semaphore, portMAX_DELAY);
	if(port->half_duplex){
		port->uart->Instance->CR1 &= ~USART_CR1_RE;
		vTaskDelay(1);
	}
	HAL_UART_Transmit_DMA(port->uart, buf, len);
	while(port->uart->hdmatx->State != HAL_DMA_STATE_READY){
		port->uart->gState = HAL_UART_STATE_READY;
		vTaskDelay(1);
	}
	if(port->half_duplex) port->uart->Instance->CR1 |= USART_CR1_RE;
	xSemaphoreGive(port->tx_semaphore);
}

static uint32_t uart_get_write_pos(port_str * port){
	return ( ((uint32_t)port->rx_buffer_size - port->uart->hdmarx->Instance->NDTR) & ((uint32_t)port->rx_buffer_size -1));
}

void ext_printf(port_str * port, const char* format, ...) {
	va_list arg;
	va_start (arg, format);
	int len;
	char send_buffer[128];
	len = vsnprintf(send_buffer, 128, format, arg);
	va_end (arg);

	if(len > 0) {
		putbuffer((unsigned char*)send_buffer, len, port);
	}
}



void task_cli(void * argument)
{
	uint32_t rd_ptr=0;
	port_str * port = (port_str*) argument;
	uint8_t * usart_rx_dma_buffer = pvPortMalloc(port->rx_buffer_size);
	HAL_UART_MspInit(port->uart);
	if(port->half_duplex){
		HAL_HalfDuplex_Init(port->uart);
	}
	HAL_UART_Receive_DMA(port->uart, usart_rx_dma_buffer, port->rx_buffer_size);
	CLEAR_BIT(port->uart->Instance->CR3, USART_CR3_EIE);

	port->tx_semaphore = xSemaphoreCreateBinary();
	xSemaphoreGive(port->tx_semaphore);

	TERMINAL_HANDLE * term_cli;

	term_cli =  TERM_createNewHandle(ext_printf, port, pdTRUE, &TERM_defaultList,NULL,"uart");

	f_mount(&SDFatFS, (TCHAR const*)SDPath, 1);

  /* Infinite loop */
	for(;;)
	{
		/* `#START TASK_LOOP_CODE` */
		while(rd_ptr != uart_get_write_pos(port)) {
			//packet_process_byte(usart_rx_dma_buffer[rd_ptr], port->phandle);
			//putbuffer(&usart_rx_dma_buffer[rd_ptr], 1, port);
			TERM_processBuffer(&usart_rx_dma_buffer[rd_ptr],1,term_cli);
			rd_ptr++;
			rd_ptr &= ((uint32_t)port->rx_buffer_size - 1);
		}

		vTaskDelay(2);
		if(ulTaskNotifyTake(pdTRUE, 1)){
			HAL_UART_MspDeInit(port->uart);
			port->task_handle = NULL;
			vPortFree(usart_rx_dma_buffer);
			vTaskDelete(NULL);
			vTaskDelay(portMAX_DELAY);
		}

	}
}

void task_cli_init(port_str * port){
	if(port->task_handle == NULL){
		xTaskCreate(task_cli, "tskCLI", 1024, (void*)port, osPriorityNormal, &port->task_handle);
	}
}

void task_cli_kill(port_str * port){
	if(port->task_handle){
		xTaskNotify(port->task_handle, 0, eIncrement);
		vTaskDelay(200);
	}
}
