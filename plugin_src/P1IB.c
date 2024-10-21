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

struct filtered
{
   /* values for each of the last 5 minutes */
   double mean[5];
   double max[5];
   double min[5];
};

struct instance
{
   struct MeterTable_entry *entry;
   CURL *curl;
   int64_t last_obis_filter_update;
   struct filtered *filter_data;
};

static double calc_mean(unsigned int num_vals, double *values)
{
   unsigned int u;
   double ret=0.0;

   for(u=0; u<num_vals; u++)
      ret += values[u];
   ret /= num_vals;
   return ret;
} /* calc_mean */

static double calc_max(unsigned int num_vals, double *values)
{
   unsigned int u;
   double ret= (num_vals ? values[0] : 0.0);

   for(u=1; u<num_vals; u++)
      if(values[u] > ret)
	 ret = values[u];
   return ret;
} /* calc_max */

static double calc_min(unsigned int num_vals, double *values)
{
   unsigned int u;
   double ret= (num_vals ? values[0] : 0.0);

   for(u=1; u<num_vals; u++)
      if(values[u] < ret)
	 ret = values[u];
   return ret;
} /* calc_min */

static void fill_obis_entry(unsigned int filter_pos,
			    struct json_object *array_json,
			    long multiplier,
			    struct obis_data *ObisEntry,
			    struct filtered *filter_data)
{
   double d[6];
   int i;

   if(ObisEntry->latest_is_valid)
   {
      ObisEntry->latest_value =
	 multiplier *
	 json_object_get_double(json_object_array_get_idx(array_json, 9));
   }
   if(filter_pos &&
      (ObisEntry->mean5m_is_valid || ObisEntry->max5m_is_valid ||
       ObisEntry->min5m_is_valid))
   {
      for(i=0; i<6; i++)
	 d[i] = json_object_get_double(json_object_array_get_idx(array_json,
					  i+filter_pos));
      if(ObisEntry->mean5m_is_valid)
      {
	 memmove(&(filter_data->mean[0]), &(filter_data->mean[1]),
		 4*sizeof(double));
	 filter_data->mean[4] = calc_mean(6, d);
	 ObisEntry->mean5m_value =
	    multiplier * calc_mean(5, filter_data->mean);
      }
      if(ObisEntry->max5m_is_valid)
      {
	 memmove(&(filter_data->max[0]), &(filter_data->max[1]),
		 4*sizeof(double));
	 filter_data->max[4] = calc_max(6, d);
	 ObisEntry->max5m_value =
	    multiplier * calc_max(5, filter_data->max);
      }
      if(ObisEntry->min5m_is_valid)
      {
	 memmove(&(filter_data->min[0]), &(filter_data->min[1]),
		 4*sizeof(double));
	 filter_data->min[4] = calc_min(6, d);
	 ObisEntry->min5m_value =
	    multiplier * calc_min(5, filter_data->min);
      }
   }
} /* fill_obis_entry */

static void fill_obis_data(int64_t obis_count,
			   struct instance *inst,
			   struct json_object *meter_json)
{
   int64_t filter_pos=0;

   if(inst && meter_json)
   {
      struct json_object *tmp_json;
      struct MeterTable_entry *entry = inst->entry;
      long multiplier = entry->MeterMultiplier;
      struct json_object *d_json = json_object_object_get(meter_json, "d");
      int i;

      if(!d_json)
	 return;

      for(i=0; i<entry->numObisEntries; i++)
      {
	 tmp_json = json_object_object_get(d_json,
					   entry->ObisEntries[i].obis_string);
	 if(tmp_json)
	    fill_obis_entry(filter_pos, tmp_json, multiplier,
			    &(entry->ObisEntries[i]), inst->filter_data);
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
	       tmp_json = json_object_object_get(info_json, "resetCnt");
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
      {{1,0,1,8,0}, "1-0:1.8.0",
       "Total active energy consumed from grid", 0, "kWh", 0,
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
   entry->numObisEntries = sizeof(driver_obis)/sizeof(struct obis_data);
   entry->ObisEntries = malloc(sizeof(driver_obis));
   if(!entry->ObisEntries)
      entry->numObisEntries = 0;
   else
      memcpy(entry->ObisEntries, driver_obis, sizeof(driver_obis));
   out->filter_data = calloc(entry->numObisEntries, sizeof(struct filtered));
   if(!out->filter_data)
       entry->numObisEntries = 0;
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
      free(i->filter_data);
      free(entry->ObisEntries);
      entry->ObisEntries = NULL;
      entry->numObisEntries = 0;
   }
} /* remove_driver */

