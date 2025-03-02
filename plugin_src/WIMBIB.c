/**************************************************************
This is the dynamic driver plugin for WIMBIB hardware listening
for wireless wmbus messages from water meters. The WIMBIB
hardware does not by definition provide OBIS data, but it is
able to provide data for most of the official OBIS values and
also some more status data.

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
#include <string.h>
#include "driver.h"
#include <curl/curl.h>
#include <json.h>
#include <pthread.h>
#include <time.h>

struct instance
{
   struct MeterTable_entry *entry;
   CURL *curl;
   int64_t last_obis_filter_update;
   long previous_volume;
   time_t previous_time;
   long average_flow;
   /* Add stuff for filtering averages here */
};

static void fill_obis_entry(struct json_object *value_json,
			    struct obis_data *ObisEntry
			    /* add stuff here for filtered data */)
{
   long multiplier = 1;

   if(ObisEntry->unit[0] != 'C')
      multiplier = 1000;
   
   if(ObisEntry->latest_is_valid)
   {
      ObisEntry->latest_value = multiplier*json_object_get_double(value_json);
   }
   if(ObisEntry->mean6m_is_valid)
   {
      ObisEntry->mean6m_value = multiplier*json_object_get_double(value_json);
   }
   if(ObisEntry->max6m_is_valid)
   {
      ObisEntry->max6m_value = multiplier*json_object_get_double(value_json);
   }
   if(ObisEntry->min6m_is_valid)
   {
      ObisEntry->min6m_value = multiplier*json_object_get_double(value_json);
   }
} /* fill_obis_entry */

static void fill_obis_data(int64_t obis_count,
			   struct instance *inst,
			   struct json_object *meter_json)
{
   if(inst && meter_json)
   {
      struct json_object *tmp_json;
      struct MeterTable_entry *entry = inst->entry;
      struct json_object *d_json = json_object_object_get(meter_json, "meter");
      int i;

      if(!d_json)
	 return;
      if(obis_count < inst->last_obis_filter_update)
      {
	 /* this will probably never happen, reset to a sane value */
	 inst->last_obis_filter_update = obis_count - 3;
      }
      else if((obis_count - inst->last_obis_filter_update) > 10)
      {
	 /* we are late to the party, lets forget what we have missed */
	 inst->last_obis_filter_update = obis_count - 10;
      }
      if((obis_count - inst->last_obis_filter_update) >= 6)
      {
	 for(i=0; i<entry->numObisEntries; i++)
	 {
	    if(!strcmp(entry->ObisEntries[i].obis_string, "8-0:2.0.0"))
	    {
	       entry->ObisEntries[i].mean6m_value = inst->average_flow;
	    }
	    else if(!strcmp(entry->ObisEntries[i].obis_string,
			    "leak-alarm burst-alarm"))
	    {
	       tmp_json =
		  json_object_object_get(d_json, "leak-alarm");
	       if(tmp_json)
	       {
		  entry->ObisEntries[i].latest_value =
		     json_object_get_boolean(tmp_json) ? 1 : 0;
		  tmp_json =
		     json_object_object_get(d_json, "burst-alarm");
		  if(tmp_json)
		  {
		     entry->ObisEntries[i].latest_value |=
			json_object_get_boolean(tmp_json) ? 2 : 0;
		  }
	       }
	    }
	    else if(!strcmp(entry->ObisEntries[i].obis_string,
			    "dry-alarm reverse-alarm"))
	    {
	       tmp_json =
		  json_object_object_get(d_json, "dry-alarm");
	       if(tmp_json)
	       {
		  entry->ObisEntries[i].latest_value =
		     json_object_get_boolean(tmp_json) ? 1 : 0;
		  tmp_json =
		     json_object_object_get(d_json, "reverse-alarm");
		  if(tmp_json)
		  {
		     entry->ObisEntries[i].latest_value |=
			json_object_get_boolean(tmp_json) ? 2 : 0;
		  }
	       }
	    }
	    else
	    {
	       tmp_json =
		  json_object_object_get(d_json,
					 entry->ObisEntries[i].obis_string);
	       if(tmp_json)
		  fill_obis_entry(tmp_json, &(entry->ObisEntries[i]));
	       if(!strcmp(entry->ObisEntries[i].obis_string,
			  "total_volume"))
	       {
		  /* update flow, but make sure that it is averaged for
		     at least 6 minutes */
		  time_t now = time(NULL);
		  
		  if((now - inst->previous_time) > 360)
		  {
		     if(inst->previous_time)
		     {
			inst->average_flow =
			   3600*(entry->ObisEntries[i].latest_value -
			       inst->previous_volume) /
			   (now - inst->previous_time);
		     }
		     inst->previous_time = now;
		     inst->previous_volume =
			entry->ObisEntries[i].latest_value;

		  }
		     
	       }
	    }
	 }
	 inst->last_obis_filter_update += 6;
      }
   }
} /* fill_obis_data */

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
		  entry->MeterType[0]=0;
		  tmp_json = json_object_object_get(info_json, "meter_model");
		  if(tmp_json)
		  {
		     strncpy(entry->MeterType,
			     json_object_get_string(tmp_json), 254);
		     entry->MeterType[254]=0;
		     entry->MeterType_len =
			strlen(entry->MeterType);
		  }
		  tmp_json = json_object_object_get(info_json, "meter_id");
		  if(tmp_json)
		  {
		     strncpy(&(entry->MeterType[strlen(entry->MeterType)]),
			     json_object_get_string(tmp_json),
			     254-strlen(entry->MeterType));
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
	       tmp_json = json_object_object_get(info_json, "crc_ok_cnt");
	       if(tmp_json)
	       {
		  fill_obis_data(json_object_get_int64(tmp_json),
				 inst,
				 meter_json);
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
   const struct obis_data driver_obis[] = {
      /* Mandatory data from this driver */
      {{8,0,1,0,0}, "total_volume",
       "Volume (V), accumulated, total, current value", 0, "L", 0,
       1, 0, 0, 0, 0, 0, 0, 0},
      {{8,0,1,2,0}, "target_volume",
       "Volume (V), accumulated, total, set date value", 0, "L", 0,
       1, 0, 0, 0, 0, 0, 0, 0},
      {{8,0,2,0,0}, "8-0:2.0.0",
       "Flow rate, average (Va/t), current value", 0, "L/h", 0,
       0, 0, 1, 0, 0, 0, 0, 0},
      /* Optional data from this driver */
      {{8,0,10,0,0}, "time_weighted_meter_temp_day",
       "Day average meter temperature", 0, "C", 0,
       0, 0, 1, 0, 0, 0, 0, 0},
      {{8,0,10,2,0}, "min_water_temp_day",
       "Day minimum water temperature", 0, "C", 0,
       0, 0, 0, 0, 0, 0, 1, 0},
      {{8,0,11,1,0}, "leak-alarm burst-alarm",
       "0=OK, 1=leak alarm, 2=burst alarm, 3=leak+burst", 0, "-", 0,
       1, 0, 0, 0, 0, 0, 0, 0},
      {{8,0,11,2,0}, "dry-alarm reverse-alarm",
       "0=OK, 1=dry alarm, 2=reverse alarm, 3=dry+reverse", 0, "-", 0,
       1, 0, 0, 0, 0, 0, 0, 0},
   };
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
#if 0
   if(pc)
   {
      pc += 11;
      entry->MeterMultiplier=atol(pc);
      if(entry->MeterMultiplier < 1)
	 entry->MeterMultiplier = 1;
   }
   else
   {
      entry->MeterMultiplier=1000;
   }
#endif
   entry->MeterMultiplier=1;
   /* initialize other parts of entry */
   entry->MeterMAC[0]=0;
   entry->MeterMAC_len=0;
   entry->numObisEntries = sizeof(driver_obis)/sizeof(struct obis_data);
   entry->ObisEntries = malloc(sizeof(driver_obis));
   if(!entry->ObisEntries)
      entry->numObisEntries = 0;
   else
      memcpy(entry->ObisEntries, driver_obis, sizeof(driver_obis));
   out->last_obis_filter_update=0;
   out->previous_volume=0;
   out->previous_time=0;
   out->average_flow=0;
   if(entry->MeterIP_len)
   {
      char url[276];
      snprintf(url, 275, "http://%s/meterData", entry->MeterIP);
      out->curl = curl_easy_init();
      curl_easy_setopt(out->curl, CURLOPT_URL, url);
      curl_easy_setopt(out->curl, CURLOPT_WRITEFUNCTION, my_curl_callback);
      curl_easy_setopt(out->curl, CURLOPT_WRITEDATA, (void *)out);
      curl_easy_perform(out->curl);
   }
   return out;
} /* init_driver */

void update_driver_data(void *driver, struct MeterTable_entry *entry)
{
   struct instance *i = driver;

   if(!i)
      return;

   curl_easy_perform(i->curl);
} /* update_driver_data */

void remove_driver(void *driver, struct MeterTable_entry *entry)
{
   struct instance *i = driver;

   if(!i)
      return;
   if (!entry)
      return;                 /* Nothing to remove */
   entry->valid=0;
   if(i->curl)
      curl_easy_cleanup(i->curl);
   i->curl = NULL;
   if(entry->numObisEntries)
   {
      free(entry->ObisEntries);
      entry->ObisEntries = NULL;
      entry->numObisEntries = 0;
   }
} /* remove_driver */

