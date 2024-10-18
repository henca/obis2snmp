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


static void *work_task(void *in)
{
   struct instance *i = in;

   while(i->cont)
   {
      curl_easy_perform(i->curl);
      sleep(10);
      /* fprintf(stderr, "work thread %ld slept\n", i->entry->MeterIndex); */
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

void *init_driver(struct MeterTable_entry *entry,
		  const char *parameters)
{
   char *pc;
   struct instance *out = malloc(sizeof(struct instance));

   if(!out)
      return NULL;

   /*   fprintf(stderr, "MeterTable_oid_len: %ld  MeterTableEntry_oid_len: %ld\n",
	MeterTable_oid_len, OID_LENGTH(MeterTableEntry_oid)); */
   
   out->entry=entry;
   
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
   out->cont=1;
   if(pthread_create(&(out->work_thread), NULL, work_task, out))
   {
      fprintf(stderr, "pthread_create failed!\n");
   }
   /* fprintf(stderr ,"End of init, index %ld listening below oid ", MeterIndex); 
      present_oid(MeterTableEntry_oid, OID_LENGTH(MeterTableEntry_oid)); */
   return out;
} /* init_driver */

void remove_driver(void *driver, struct MeterTable_entry *entry)
{
   struct instance *i = driver;

   if(!i)
      return;
   if (!entry)
      return;                 /* Nothing to remove */
   if(entry->valid == 1)
   {
   }
   entry->valid=0;
   if(i->curl)
      curl_easy_cleanup(i->curl);
   i->cont = 0;
} /* remove_driver */

