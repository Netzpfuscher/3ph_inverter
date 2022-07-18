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
#include "lwip.h"
#include "socket.h"
#include "dns.h"

void putbuffer(unsigned char *buf, unsigned int len, port_str * port){
	xSemaphoreTake(port->tx_semaphore, portMAX_DELAY);
	if(port->half_duplex){
		port->uart->Instance->CR1 &= ~USART_CR1_RE;
		vTaskDelay(1);
	}
	HAL_UART_Transmit_DMA(port->uart, buf, len);
	vTaskDelay(1);
	while(port->uart->gState == HAL_UART_STATE_BUSY_TX){
		vTaskDelay(1);
	}

	if(port->half_duplex) port->uart->Instance->CR1 |= USART_CR1_RE;
	xSemaphoreGive(port->tx_semaphore);
}

static uint32_t uart_get_write_pos(port_str * port){
	return ( ((uint32_t)port->rx_buffer_size - __HAL_DMA_GET_COUNTER(port->uart->hdmarx)) & ((uint32_t)port->rx_buffer_size -1));
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


void ext_printnet(int * port, const char* format, ...) {
	va_list arg;
	va_start (arg, format);
	int len;
	char send_buffer[128];
	len = vsnprintf(send_buffer, 128, format, arg);
	va_end (arg);

	if(len > 0) {
		write(*port,send_buffer,len);
	}
}

static uint32_t IPaddress = 0;

#define MAX 128

void task_client(void * argument)
{
	int connfd = *((int*)argument);
    char buff[MAX];
    int n;
    // infinite loop for chat
    printf("Start Task...\r\n");
    TERMINAL_HANDLE * handle = TERM_createNewHandle(ext_printnet, argument, pdTRUE, &TERM_defaultList,NULL,"net");
    for (;;) {

        n = recv(connfd, buff, sizeof(buff), MSG_DONTWAIT);
        // read the message from client and copy it in buffer
        // print buffer which contains the client contents
        if(n > 0){
        	TERM_processBuffer(&buff,n,handle);
        }
        if(n == 0){
        	break;
        }


        vTaskDelay(5);
    }
    printf("Delete Task...\r\n");
    close(connfd);
    vPortFree(argument);
    vTaskDelete(NULL);

}


#define SA struct sockaddr
int sockfd;
// Driver function
int eth_bind(){
    int connfd, len;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(7);
    // Binding newly created socket to given IP and verification
    if (( bind(sockfd, (SA*)&servaddr, sizeof(servaddr)) ) != 0)
    {
        printf("socket bind failed...\n");
        return;
    }
    else
        printf("Socket successfully binded..\n");

    // Now server is ready to listen and verification
    if ((listen(sockfd, 5)) != 0) {
        printf("Listen failed...\n");
        return;
    }
    else
        printf("Server listening..\n");
}

void task_server(void * argument)
{
	while(1){
		struct sockaddr_in cli;
		int connfd;

		int len = sizeof(cli);
		connfd = accept(sockfd, (SA*)&cli, &len);
		if (connfd < 0) {
			printf("server accept failed...\n");
		}
		else
		{	printf("server accept the client...\n");

		// Function for chatting between client and server
		int* dat = pvPortMalloc(sizeof(int));
		*dat = connfd;
		xTaskCreate(task_client, "tskClient", 1024, dat, osPriorityNormal, NULL);
		}
	}
}


void help_ifconfig(TERMINAL_HANDLE * handle){
    ttprintf("\teth0 [ip]\r\n");
    ttprintf("\tnetmask [netmask]\r\n");
    ttprintf("\tgw [netmask]\r\n");
    ttprintf("\tdhcp\r\n");
}

uint8_t CMD_ifconfig(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args){
    uint8_t currArg = 0;
    ip4_addr_t ip = *netif_ip4_addr(netif_default);
    ip4_addr_t netmask = *netif_ip4_netmask(netif_default);
    ip4_addr_t gw = *netif_ip4_gw(netif_default);
    for(;currArg<argCount; currArg++){
        if(strcmp(args[currArg], "-?") == 0){
            help_ifconfig(handle);
            return TERM_CMD_EXIT_SUCCESS;
        }
        if(strcmp(args[currArg], "eth0") == 0){
            if(++currArg<argCount){
                if(ipaddr_aton(args[currArg], &ip)){
                	dhcp_stop(netif_default);
                	netif_set_addr(netif_default, &ip, &netmask, &gw);
                }else{
                    help_ifconfig(handle);
                    return TERM_CMD_EXIT_SUCCESS;
                }
            }else{
               help_ifconfig(handle);
               return TERM_CMD_EXIT_SUCCESS;
            }
        }
        if(strcmp(args[currArg], "netmask") == 0){
            if(++currArg<argCount){
            	if(ipaddr_aton(args[currArg], &netmask)){
            		dhcp_stop(netif_default);
            		netif_set_addr(netif_default, &ip, &netmask, &gw);
                }else{
                    help_ifconfig(handle);
                    return TERM_CMD_EXIT_SUCCESS;
                }
            }else{
                help_ifconfig(handle);
                return TERM_CMD_EXIT_SUCCESS;
            }
        }
        if(strcmp(args[currArg], "gw") == 0){
            if(++currArg<argCount){
            	if(ipaddr_aton(args[currArg], &gw)){
            		dhcp_stop(netif_default);
            		netif_set_addr(netif_default, &ip, &netmask, &gw);
                }else{
                    help_ifconfig(handle);
                    return TERM_CMD_EXIT_SUCCESS;
                }
            }else{
                help_ifconfig(handle);
                return TERM_CMD_EXIT_SUCCESS;
            }
        }
        if(strcmp(args[currArg], "dhcp") == 0){
        	dhcp_start(netif_default);
        	ttprintf("Activating DHCP...\r\n");
        	vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    char buff[16];
    ttprintf("eth0\r\x1b[%dCLink type: Ethernet  HWaddr %02x:%02x:%02x:%02x:%02x:%02x\r\n", 7, netif_default->hwaddr[0], netif_default->hwaddr[1], netif_default->hwaddr[2], netif_default->hwaddr[3], netif_default->hwaddr[4], netif_default->hwaddr[5]);
    ipaddr_ntoa_r(&ip, buff, sizeof(buff));
    ttprintf("\x1b[%dCinet addr: %s ", 7, buff);
    ipaddr_ntoa_r(&netmask, buff, sizeof(buff));
    ttprintf("Mask: %s\r\n", buff);
    ipaddr_ntoa_r(&gw, buff, sizeof(buff));
    ttprintf("\x1b[%dCgateway: %s ", 7, buff);
    //FreeRTOS_inet_ntoa(ulDNSServerAddress, buff);
    //ttprintf("dns: %s\r\n\n", buff);

    return TERM_CMD_EXIT_SUCCESS;
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

	TERM_addCommand(CMD_ifconfig, "ifconfig", "IPIP", 0, &TERM_defaultList);

	f_mount(&SDFatFS, (TCHAR const*)SDPath, 1);

	eth_bind();

	xTaskCreate(task_server, "tskServ", 256, NULL, osPriorityNormal, NULL);

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
