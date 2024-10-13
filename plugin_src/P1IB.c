/**************************************************************
This is the dynamic driver plugin for P1IB hardware connected
by HAN port to electricity meter.

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
#include <string.h>
#include "driver.h"
#include <net-snmp/agent/util_funcs.h>
#include <curl/curl.h>
#include <json.h>
#include <pthread.h>

struct instance
{
   struct MeterTable_entry *entry;
   CURL *curl;
   int cont;
   pthread_t work_thread;
};

static struct MeterTable_entry *pMeterEntries=NULL;
static unsigned int MaxRegisteredEntry=0;
static unsigned int numRegisteredEntries=0;

static void *work_task(void *in)
{
   struct instance *i = in;

   while(i->cont)
   {
      curl_easy_perform(i->curl);
      sleep(10);
      fprintf(stderr, "work thread %ld slept\n", i->entry->MeterIndex);
   }
   free(i);
   fprintf(stderr, "work thread done\n");
   return NULL;
} /* work_task */

static size_t my_curl_callback(void *buffer, size_t size, size_t nmemb, void *userp)
{
   static size_t pos = 0;
   size_t out = size*nmemb;
   struct instance *inst=userp;
   struct MeterTable_entry *entry;
   char data[CURL_MAX_WRITE_SIZE+1];

   if(!inst) /* sanity check */
      return 0;
   entry = inst->entry;
   if((pos + out)<=CURL_MAX_WRITE_SIZE)
   {      
      memcpy(&data[pos], buffer, out);
      pos += out;
      data[pos]=0;
      if((pos > 4) && (data[pos-1]== '}') && (data[pos-2]== '}'))
      {
	 struct json_object *meter_json, *info_json, *tmp_json;
	 pos = 0;
	 meter_json = json_tokener_parse(data);
	 if(meter_json)
	 {
	    info_json = json_object_object_get(meter_json, "info");
	    if(info_json)
	    {
	       if(!entry->MeterType_len)
	       {
		  tmp_json = json_object_object_get(info_json, "meter");
		  if(tmp_json)
		  {
		     strncpy(entry->MeterType,
			     json_object_get_string(tmp_json), 254);
		     entry->MeterType[254]=0;
		     entry->MeterType_len =
			strlen(entry->MeterType);
		  }
	       }
	       if(!entry->MeterMAC_len)
	       {
		  tmp_json = json_object_object_get(info_json, "mac");
		  if(tmp_json)
		  {
		     strncpy(entry->MeterMAC,
			     json_object_get_string(tmp_json), 254);
		     entry->MeterMAC[254]=0;
		     entry->MeterMAC_len = strlen(entry->MeterMAC);
		  }
			
	       }
	       tmp_json = json_object_object_get(info_json, "rssi");
	       if(tmp_json)
	       {
		  entry->MeterRSSI = json_object_get_int(tmp_json);
	       }
	    }
	    json_object_put(meter_json); /* free json stuff */
	 }
      }
      return out;
   }
   else
      return 0;
   
} /* my_curl_callback */

static u_char *
agent_h_meter(struct variable *vp, oid *name, size_t *length, int exact,
    size_t *var_len, WriteMethod **write_method)
{
   static unsigned long long_ret;
   unsigned int index;
   struct MeterTable_entry *entry;

   if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
      return NULL;
   index = name[*length -1];
   /* fprintf(stderr, "index: %d\n", index); */
   if(index > MaxRegisteredEntry) return NULL;
   entry = &(pMeterEntries[index-1]);
   /* fprintf(stderr, "low enough\n", index); */
   if((entry->MeterIndex != index) || !(entry->valid)) return NULL;
   
   /* fprintf(stderr, "vp: %p\n", vp);
   fprintf(stderr, "magic: %d\n", vp->magic);
   fprintf(stderr, "lengthp: %p\n", length);
   fprintf(stderr, "length: %d\n", *length);
   fprintf(stderr, "oid: ");
   for(long_ret=0; long_ret<*length; long_ret++) 
   fprintf(stderr, "%d.", name[long_ret]); */
   switch (vp->magic) {
      case COLUMN_METERINDEX:
	 long_ret  = index;
	 return (u_char *)&long_ret;
      case COLUMN_METERTYPE:
	 if(!entry->MeterType_len)
	    return NULL;
	 else
	 {
	    *var_len = entry->MeterType_len;
	    return (u_char *) entry->MeterType;
	 }
      case COLUMN_METERIP:
	 *var_len = entry->MeterIP_len;
	 return (u_char *) entry->MeterIP;
      case COLUMN_METERMAC:
	 if(!entry->MeterMAC_len)
	    return NULL;
	 else
	 {
	    *var_len = entry->MeterMAC_len;
	    return (u_char *) entry->MeterMAC;
	 }
      case COLUMN_METERRSSI:
	 long_ret  = entry->MeterRSSI;
	 return (u_char *)&long_ret;
      case COLUMN_METERMULTIPLIER:
	 long_ret  = entry->MeterMultiplier;
	 return (u_char *)&long_ret;
      default:
	 break;
   }
   return NULL;
} /* agent_h_meter */

struct variable8 agent_meter_vars[] = {
   { COLUMN_METERINDEX, ASN_INTEGER, RONLY, agent_h_meter, 1, { COLUMN_METERINDEX } },
   { COLUMN_METERTYPE, ASN_OCTET_STR, RONLY, agent_h_meter, 1, { COLUMN_METERTYPE } },
   { COLUMN_METERIP, ASN_OCTET_STR, RONLY, agent_h_meter, 1, { COLUMN_METERIP } },
   { COLUMN_METERMAC, ASN_OCTET_STR, RONLY, agent_h_meter, 1, { COLUMN_METERMAC } },
   { COLUMN_METERRSSI, ASN_INTEGER, RONLY, agent_h_meter, 1, { COLUMN_METERRSSI } },
   { COLUMN_METERMULTIPLIER, ASN_INTEGER, RONLY, agent_h_meter, 1, { COLUMN_METERMULTIPLIER } },
};

int
driver_handler(netsnmp_mib_handler *handler,
	       netsnmp_handler_registration *reginfo,
	       netsnmp_agent_request_info *reqinfo,
	       netsnmp_request_info *requests)
{
    netsnmp_request_info *request;

    DEBUGMSGTL(("Meters:handler", "Processing request (%d)\n",
                reqinfo->mode));
    snmp_log(LOG_INFO,"P1IB handler....\n");
    switch (reqinfo->mode) {
        /*
         * Read-support (also covers GetNext requests)
         */
    case MODE_GET:
        for (request = requests; request; request = request->next) {
            if (request->processed)
                continue;
	    snmp_set_var_typed_integer(request->requestvb, ASN_INTEGER,
				       7 /* request->index */);
        }
        break;

    }
    return SNMP_ERR_NOERROR;
} /* driver_handler */

#if 0
static void present_oid(oid o[], size_t l)
{
   size_t i;

   for(i=0; i<l; i++)
   {
      fprintf(stderr, "%ld", o[i]);
      if(i<(l-1))
	 fprintf(stderr, ".");
   }
   fprintf(stderr, "\n");
} /* present_oid */
#endif

void *init_driver(long MeterIndex,
		  const char *parameters)
{
   struct MeterTable_entry *entry=NULL;
   oid       MeterTableEntry_oid[MeterTable_oid_len+1];
   char *pc;
   struct instance *out = malloc(sizeof(struct instance));

   if(!out)
      return NULL;

   memcpy(MeterTableEntry_oid, MeterTable_oid, MeterTable_oid_len*sizeof(oid));
   MeterTableEntry_oid[MeterTable_oid_len] = 1;

   /*   fprintf(stderr, "MeterTable_oid_len: %ld  MeterTableEntry_oid_len: %ld\n",
	MeterTable_oid_len, OID_LENGTH(MeterTableEntry_oid)); */
   
   snmp_log(LOG_INFO,"Start of init.\n");
   if(MeterIndex < 1)
   {
      free(out);
      return NULL;
   }
   if(MeterIndex > MaxRegisteredEntry)
   {
      MaxRegisteredEntry = MeterIndex;
      entry = pMeterEntries;
      pMeterEntries = realloc(pMeterEntries, MeterIndex*sizeof(struct MeterTable_entry));
      numRegisteredEntries++;
   }
   if(!pMeterEntries)
   {
      pMeterEntries = entry;
      numRegisteredEntries--;
      free(out);
      return NULL;
   }
   entry = &(pMeterEntries[MeterIndex-1]);
   out->entry=entry;
   
   entry->MeterIndex = MeterIndex;
   entry->valid = 1;
   pc = strstr(parameters, "ip=");
   if(pc)
   {
      pc += 3;
      strncpy(entry->MeterIP, pc, 255);
      entry->MeterIP[254]=0;
      pc=strchr(entry->MeterIP, ',');
      if(pc)
	 *pc=0;
      entry->MeterIP_len = strlen(entry->MeterIP);
   }
   else
   {
      entry->MeterIP_len=0;
      entry->MeterIP[0]=0;
   }
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
      entry->MeterMultiplier=1;
   }
   /* initialize other parts of entry */
   entry->MeterMAC[0]=0;
   entry->MeterMAC_len=0;
   if(entry->MeterIP_len)
   {
      char url[256];
      snprintf(url, 255, "http://%s/meterData", entry->MeterIP);
      out->curl = curl_easy_init();
      curl_easy_setopt(out->curl, CURLOPT_URL, url);
      curl_easy_setopt(out->curl, CURLOPT_WRITEFUNCTION, my_curl_callback);
      curl_easy_setopt(out->curl, CURLOPT_WRITEDATA, (void *)out);
      curl_easy_perform(out->curl);
   }
   /* REGISTER_MIB("Meter", agent_meter_vars, variable8, MeterTableEntry_oid); */
   if (register_mib_range("Meter",
			  (struct variable *) agent_meter_vars,
			  sizeof(struct variable8),
			  sizeof(agent_meter_vars)/sizeof(struct variable8),
			  MeterTableEntry_oid,
			  sizeof(MeterTableEntry_oid)/sizeof(oid),
			  DEFAULT_MIB_PRIORITY,
			  MeterIndex,
			  MeterIndex+1,
			  NULL) !=
       MIB_REGISTERED_OK)
   {
      DEBUGMSGTL(("register_mib", "%s registration failed\n", descr));
   } 
   /*   netsnmp_register_handler(
      netsnmp_create_handler_registration("MeterTableEntry",
					  driver_handler,
					  MeterTableEntry_oid,
					  OID_LENGTH(MeterTableEntry_oid),
					  HANDLER_CAN_RONLY)); */
   out->cont=1;
   if(pthread_create(&(out->work_thread), NULL, work_task, out))
   {
      fprintf(stderr, "pthread_create failed!\n");
   }
   else
      fprintf(stderr, "pthread success, index %ld\n", out->entry->MeterIndex);
   /* fprintf(stderr ,"End of init, index %ld listening below oid ", MeterIndex); 
      present_oid(MeterTableEntry_oid, OID_LENGTH(MeterTableEntry_oid)); */
   return out;
} /* init_driver */

static void final_cleanup(void)
{
   free(pMeterEntries);
   pMeterEntries=NULL;
} /* final_cleanup */

void remove_driver(void *driver)
{
   struct instance *i = driver;
   struct MeterTable_entry *entry;

   if(!i)
      return;
   entry=i->entry;
   if (!entry)
      return;                 /* Nothing to remove */
   if(entry->valid == 1)
   {
   }
   entry->valid=0;
   if(i->curl)
      curl_easy_cleanup(i->curl);
   numRegisteredEntries--;
   i->cont = 0;
   if(!numRegisteredEntries)
      final_cleanup();
   /* !!! - release any other internal resources (internal_data) */
} /* remove_driver */

