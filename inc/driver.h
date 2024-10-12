/**************************************************************
This file describes the interface that different meter drivers
are supposed to use to publish their SNMP data.

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
#define DRIVER_H

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/*
 * column number definitions for table MeterTable 
 */
#define COLUMN_METERINDEX               1
#define COLUMN_METERTYPE                2
#define COLUMN_METERIP                  3
#define COLUMN_METERMAC                 4
#define COLUMN_METERRSSI                5
#define COLUMN_METERMULTIPLIER		6
#define COLUMN_METEROBISDESCRIPTION    	7
#define COLUMN_METEROBISUNIT		8
#define COLUMN_METEROBISLATEST		9
#define COLUMN_METEROBIS5MINMEAN       	10
#define COLUMN_METEROBIS5MINMAX		11
#define COLUMN_METEROBIS5MINMIN		12

#define MeterTable_oid (const oid[]){ 1, 3, 6, 1, 4, 1, 62368, 1 }
#define MeterTable_oid_len (size_t)OID_LENGTH(MeterTable_oid)

struct MeterTable_entry {
    /*
     * Index values 
     */
    long            MeterIndex;

    /*
     * Column values 
     */
    char            MeterType[255];
    size_t          MeterType_len;
    char            MeterIP[255];
    size_t          MeterIP_len;
    char            MeterMAC[255];
    size_t          MeterMAC_len;
    long            MeterRSSI;
    long            MeterMultiplier;

    int             valid;
};

extern void *init_driver(long MeterIndex,
				const char *parameters);
void remove_driver(void *driver);

#endif
