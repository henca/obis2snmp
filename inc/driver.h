/**************************************************************
This file describes the interface that different meter drivers
are supposed to use to publish their data.

SPDX-License-Identifier: BSD-2-Clause

Copyright (c) 2024, Henrik Carlqvist

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************/

#ifndef DRIVER_H
#define DRIVER_

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

struct obis_data {
   oid obis_oid[5];       /* mandatory {A,B,C,D} A-B:C.D.E */
   char obis_string[50];  /* optional for drivers internal use */
   char description[255]; /* mandatory description of value */
   size_t description_len;/* will later be set by calling agent */
   char unit[255];        /* mandatory unit of original obis float value */
   size_t unit_len;       /* will later be set by calling agent */
   int latest_is_valid;   /* mandatory, 0 if not used latest */
   long latest_value;     /* rounded obis float*MeterMultiplier if valid */
   int mean5m_is_valid;   /* mandatory, 0 if not used 5 minute mean */
   long mean5m_value;     /* rounded mean float*MeterMultiplier if valid */
   int max5m_is_valid;    /* mandatory, 0 if not used 5 minute max */
   long max5m_value;      /* rounded max float*MeterMultiplier if valid */
   int min5m_is_valid;    /* mandatory, 0 if not used 5 minute min */
   long min5m_value;      /* rounded min float*MeterMultiplier if valid */
};

struct MeterTable_entry {
   char            MeterType[255]; /* set by init_driver if used */
   size_t          MeterType_len; /* set to strlen(MeterType) by init_driver */
   char            MeterIP[255]; /* set by init_driver if used */
   size_t          MeterIP_len; /* set to strlen(MeterIP) by init_driver */
   char            MeterMAC[255]; /* set by init_driver if used */
   size_t          MeterMAC_len; /* set to strlen(MeterMAC) by init_driver */
   long            MeterRSSI; /* set to nonzero by init_driver if used,
				 updated by update_data_data */
   long            MeterMultiplier;  /* set by init_driver */
   unsigned int    numObisEntries; /* set by init_driver */
   struct obis_data *ObisEntries; /* allocated and set by init_driver,
				     updated by update_driver_data and
				     freed by remove_driver */

   int             valid; /* set to non zero by init_driver at success */
};

extern void *init_driver(struct MeterTable_entry *out_data,
			 const char *parameters);
void update_driver_data(void *driver, struct MeterTable_entry *work_data);
void remove_driver(void *driver, struct MeterTable_entry *work_data);

#endif
