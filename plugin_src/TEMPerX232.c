/**************************************************************
This is the dynamic driver plugin for TemperX232 USB thermometer and humidity
sensor. The TemperX232 does not provide OBIS data, but its abiliity to
measure both indoor and outdoor temperatures gives data useful for energy
consumtion  statistics.

SPDX-License-Identifier: BSD-2-Clause

Copyright (c) 2025, Henrik Carlqvist

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
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include "driver.h"

#define MAX_TEMPER_VALUES 10

struct data
{
   char description[20];
   float value;
   char unit[5];
};

struct instance
{
   struct MeterTable_entry *entry;
   int fdTtyUSB;
   /* Add stuff for filtering averages here */
   char description[MAX_TEMPER_VALUES][20];
   float average[MAX_TEMPER_VALUES];
};

/* returns file descriptor or < 0 at failure */
static int init_serial(const char *port)
{
   struct termios t;
   int fd = open(port, O_RDWR | O_NOCTTY);

   if(fd < 0)
      return fd;
   if(flock(fd, LOCK_EX | LOCK_NB))
   {
      close(fd);
      return -1;
   }
   if(tcgetattr(fd, &t))
   {
      close(fd);
      return -1;
   }
   cfsetispeed(&t, B9600); 
   cfsetospeed(&t, B9600);
   /* echo might mess things up */
   t.c_iflag &= ~ECHO;
   t.c_oflag &= ~ECHO;
   t.c_cflag &= ~ECHO; 
   t.c_lflag &= ~ECHO;
   t.c_lflag &= ~ICANON;
   t.c_cc[VTIME]=5;
   t.c_cc[VMIN]=0;
   tcsetattr(fd, TCSANOW, &t);

   return fd; 
}

/* returns length of string or 0 at failure */
static int get_version(int fd, char *buf, int maxlen)
{
   int out=0;
   const char command[] = "Version\n";
   (void)! write(fd, command, strlen(command));
   while(maxlen > 0)
   {
      if(read(fd, buf, 1))
      {
	 if(*buf++ > 0x0d) /* strip EOL */
	 {
	    maxlen--;
	    out++;
	 }
	 else
	    buf--;
      }
      else
      {
	 *buf = 0;
	 return out;
      }
   }
   /* Whoops, this should not happen, lets start by emptying from any remains
      of the response */
   while(read(fd, buf, 1));
   /* We should also make sure that output is zero-terminated */
   if(out)
   {
      buf--;
      out--;
      *buf=0;
   }
   return out;
}

static void sort_data(struct data *d, int numdata)
{
   struct data t;
   int i;
   int cont=1;
   while(cont)
   {
      cont=0;
      /* kind of bubble sort */
      for(i=0; i< numdata-1; i++)
	 if(strcmp(d[i].description, d[i+1].description)>0)
	 {
	    t = d[i];
	    d[i] = d[i+1];
	    d[i+1] = t;
	    cont = 1;
	 }
   }
} /* sort_data */

static int fill_data(const char *buf, struct data *d, int maxnum)
{
   int numdata=0;
   char *p;

   while((numdata<maxnum) && (p=strstr(buf, "Temp-")))
   {
      strncpy(d[numdata].description, p, 19);
      d[numdata].description[19]=0;
      buf=p;
      if((p=strstr(d[numdata].description, ":")))
	 *p=0;
      if((p=strstr(buf, ":")))
      {
	 p++;
	 d[numdata].value=atof(p);
	 buf=p;
	 if((p=strstr(buf, "[")))
	 {
	    p++;
	    strncpy(d[numdata].unit, p, 4);
	    d[numdata].unit[4]=0;
	    buf=p;
	    if((p=strstr(d[numdata].unit, "]")))
	       *p = 0;
	    if((p=strstr(buf, "]")))
	    {
	       p++;
	       numdata++;
	       if(*p ==',')
	       {
		  p++;
		  buf=p;
		  snprintf(d[numdata].description, 20, "Humidity-%s",
			   &(d[numdata-1].description[5]));
		  d[numdata].value=atof(p);
		  if((p=strstr(buf, "[")))
		  {
		     p++;
		     strncpy(d[numdata].unit, p, 4);
		     d[numdata].unit[4]=0;
		     buf=p;
		     if((p=strstr(d[numdata].unit, "]")))
			*p = 0;
		     if((p=strstr(buf, "]")))
		     {
			p++;
			numdata++;
		     }
		  }
	       }
	    }
	 }
      }
   }
   sort_data(d, numdata);
   return numdata;
}
/* returns number of sorted data fields or 0 at failure */
static int get_data(int fd, struct data *d, int maxnum)
{
   int out=0;
   const char command[] = "ReadTemp\n";
   char b[500];
   char *buf=b;
   int maxlen = 500;
   
   (void)!write(fd, command, strlen(command));
   while(maxlen > 0)
   {
      if(read(fd, buf, 1))
      {
	 if(*buf > 0x0d) /* strip EOL */
	 {
	    buf++;
	    maxlen--;
	    out++;
	 }
      }
      else
      {
	 *buf = 0;
	 return fill_data(b, d, maxnum);
      }
   }
   /* Whoops, this should not happen, lets start by emptying from any remains
      of the response */
   while(read(fd, buf, 1));
   /* We should also make sure that output is zero-terminated */
   if(out)
   {
      buf--;
      out--;
      *buf=0;
   }
   return fill_data(b, d, maxnum);
}

#if 0
int main(int argc, char **argv) /* remove this main function later, now only
				   for testing purposes */
{
   char version[50];
   struct data d[MAX_TEMPER_VALUES];
   int numdata;
   int i;
   int fd;
   if(argc < 2)
      return  1;
   if(!((fd = init_serial(argv[1])) > 0))
      return 2;
   get_version(fd, version, 50);
   printf("%s\n", version);
   numdata=get_data(fd, d, MAX_TEMPER_VALUES);
   for(i=0; i<numdata; i++)
      printf("%20s : %5.2f %s\n", d[i].description, d[i].value, d[i].unit);

   return 0;
} /* main, to be removed!!! */

#endif

void *init_driver(struct MeterTable_entry *entry,
		  const char *parameters)
{
   char *pc;
   struct obis_data driver_obis[MAX_TEMPER_VALUES];
   struct data d[MAX_TEMPER_VALUES];
   int numdata=0;
   int i;

   struct instance *out = malloc(sizeof(struct instance));

   if(!out)
      return NULL;

   /*   fprintf(stderr, "MeterTable_oid_len: %ld  MeterTableEntry_oid_len: %ld\n",
	MeterTable_oid_len, OID_LENGTH(MeterTableEntry_oid)); */
   
   out->entry=entry;
   
   entry->valid = 1;
   pc = strstr(parameters, "device=");
   if(pc)
   {
      pc += 7;
      strncpy(entry->MeterIP, pc, 255);
      entry->MeterIP[254]=0;
      entry->MeterIP_len = strlen(entry->MeterIP);
   }
   else
   {
      sprintf(entry->MeterIP, "/dev/ttyUSB0");
      entry->MeterIP_len = strlen(entry->MeterIP);
   }
   out->fdTtyUSB = init_serial(entry->MeterIP);
   if(out->fdTtyUSB < 0)
   {
      free(out);      
      return NULL;
   }
   entry->MeterType_len =
      get_version(out->fdTtyUSB, entry->MeterType, 254);
   numdata=get_data(out->fdTtyUSB, d, MAX_TEMPER_VALUES);
   if(!numdata)
   {
      free(out);      
      return NULL;
   }
   for(i=0; i<numdata; i++)
   {
      driver_obis[i].obis_oid[0] = 0;
      driver_obis[i].obis_oid[1] = i;
      driver_obis[i].obis_oid[2] = 10;
      driver_obis[i].obis_oid[3] = 0;
      driver_obis[i].obis_oid[4] = 0;
      snprintf(driver_obis[i].obis_string, 50, "%s", d[i].description);
      snprintf(driver_obis[i].description, 255, "%s", d[i].description);
      snprintf(driver_obis[i].unit, 255, "%s", d[i].unit);
      driver_obis[i].latest_is_valid = 1;
      strcpy(out->description[i], d[i].description);
      out->average[i] = d[i].value;
   }
   for(;i<MAX_TEMPER_VALUES;i++)
      out->description[i][0]=0;
   pc = strstr(parameters, "multiplier=");
   if(pc)
   {
      pc += 11;
      entry->MeterMultiplier=atol(pc);
      if(entry->MeterMultiplier < 1)
	 entry->MeterMultiplier = 1;
   }
   else
   {
      entry->MeterMultiplier=100;
   }
   for(i=0; i<numdata; i++)
   {
      driver_obis[i].obis_oid[0] = 0;
      driver_obis[i].obis_oid[1] = i;
      driver_obis[i].obis_oid[2] = 10;
      driver_obis[i].obis_oid[3] = 0;
      driver_obis[i].obis_oid[4] = 0;
      snprintf(driver_obis[i].obis_string, 50, "%s", d[i].description);
      snprintf(driver_obis[i].description, 255, "%s", d[i].description);
      snprintf(driver_obis[i].unit, 255, "%s", d[i].unit);
      driver_obis[i].latest_is_valid = 1;
      driver_obis[i].latest_value = entry->MeterMultiplier * d[i].value;
      driver_obis[i].mean6m_is_valid = 1;
      driver_obis[i].mean6m_value = entry->MeterMultiplier * out->average[i];
      driver_obis[i].max6m_is_valid = 0;
      driver_obis[i].min6m_is_valid = 0;
   }

   /* initialize other parts of entry */
   entry->MeterMAC[0]=0;
   entry->MeterMAC_len=0;

   entry->MeterRSSI=0; /* not used */
			
   entry->numObisEntries = numdata;
   entry->ObisEntries = malloc(numdata*sizeof(struct obis_data));
   if(!entry->ObisEntries)
      entry->numObisEntries = 0;
   else
      memcpy(entry->ObisEntries, driver_obis,
	     numdata*sizeof(struct obis_data));
   return out;
} /* init_driver */

void update_driver_data(void *driver, struct MeterTable_entry *entry)
{
   struct data d[MAX_TEMPER_VALUES];
   int numdata=0;
   int m,n;
   struct instance *i = driver;

   if(!i)
      return;
   numdata=get_data(i->fdTtyUSB, d, MAX_TEMPER_VALUES);
   /* Both arrays should be sorted and contain the same descriptions, but
      if something would be missing somewhere we just skip that update */
   for(m=0, n=0; (m<numdata) && (n<i->entry->numObisEntries); m++, n++)
      if(!strcmp(d[m].description, i->entry->ObisEntries[n].obis_string))
      {
	 i->entry->ObisEntries[n].latest_value =
	    i->entry->MeterMultiplier * d[m].value;
	 i->average[n] = 0.95*i->average[n] + 0.05*d[m].value;
	 i->entry->ObisEntries[n].mean6m_value =
	    i->entry->MeterMultiplier * i->average[n];
      }
      else if(0>strcmp(d[m].description, i->entry->ObisEntries[n].obis_string))
	 n--;
      else
	 m--;
   for(m=0, n=0; (m<numdata) && (n<i->entry->numObisEntries); m++, n++)
      if(!strcmp(d[m].description, i->description[n]))
	 i->average[n] = 0.9*i->average[n] + 0.1*d[m].value;
      else if(0>strcmp(d[m].description, i->entry->ObisEntries[n].obis_string))
	 n--;
      else
	 m--;
} /* update_driver_data */

void remove_driver(void *driver, struct MeterTable_entry *entry)
{
   struct instance *i = driver;

   if(!i)
      return;
   if (!entry)
      return;                 /* Nothing to remove */
   entry->valid=0;
   close(i->fdTtyUSB);
   if(entry->numObisEntries)
   {
      free(entry->ObisEntries);
      entry->ObisEntries = NULL;
      entry->numObisEntries = 0;
   }
} /* remove_driver */
