/*
 * init.c
 *
 *  Created on: Jul 9, 2022
 *      Author: jensk
 */

#include "usart.h"
#include "task_cli.h"

port_str main_uart = {	.uart = &huart1,
					    .rx_buffer_size = 512,
						.half_duplex = false,
						.task_handle = NULL
};

void init_system(void){

	task_cli_init(&main_uart);

}
