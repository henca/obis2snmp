#ifndef DRIVER_H
#define DRIVER_H

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/*
 * column number definitions for table MeterTable 
 */
#define COLUMN_METERINDEX               1
#define COLUMN_METERIDENTIFICATION      2
#define COLUMN_METERIP                  3
#define COLUMN_METERMAC                 4
#define COLUMN_METERRSSI                5
#define COLUMN_METERMULTIPLIER		6
#define COLUMN_METEROBISDESCRIPTION		7
#define COLUMN_METEROBISUNIT		8
#define COLUMN_METEROBISLATEST		9
#define COLUMN_METEROBIS5MINMEAN		10
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
    char            MeterIdentification[255];
    size_t          MeterIdentification_len;
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
