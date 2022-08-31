/*
 * Copyright (c) 2014, Texas Instruments Incorporated - http://www.ti.com/
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*---------------------------------------------------------------------------*/
/**
 *
 * Room sensor for SBAN project.
 *
 */
/*---------------------------------------------------------------------------*/

#include "contiki.h"
#include <assert.h>
#include "./room-sensor.h"
int sensorId;

#if CONTIKI_TARGET_COOJA
extern int simMoteID;
#endif
struct etimer et;

void initialize();
void state_machine();

PROCESS_NAME(room_sensor_process);
AUTOSTART_PROCESSES(&room_sensor_process);
PROCESS(room_sensor_process, "Room sensor");
PROCESS_THREAD(room_sensor_process, ev, data)
{
  PROCESS_BEGIN();

  #if CONTIKI_TARGET_COOJA
  sensorId = simMoteID;
  #else
  LOG_ERR("Function only with cooja target");
  PROCESS_EXIT();
  #endif

  LOG_INFO("Room Sensor Process %d\n", sensorId);

  initialize();
  etimer_set(&et, 0);

  while(1) {

    PROCESS_YIELD();

    if ((ev == PROCESS_EVENT_TIMER && data == &et) || ev == PROCESS_EVENT_POLL) {
      state_machine();
    }

  }

  PROCESS_END();
}
