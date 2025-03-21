/**************************************************************
This is the main file of the obis2snmp agentx proxy

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

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <json.h>
#include <unistd.h>
#include <signal.h>
#include <dlfcn.h>
#include <libgen.h>
#include <time.h>
#include <curl/curl.h>

#include "driver.h"
#include "obis2snmp.h"
#include <net-snmp/agent/util_funcs.h>

#define VERSION_STRING "1.2beta"

struct driver_data {
   void *dlhandle;
   void *instance;
   void *(*init_driver)(struct MeterTable_entry *, const char *);
   void (*update_driver_data)(void *, struct MeterTable_entry *);
   void (*remove_driver)(void *, struct MeterTable_entry *);
};

static int keep_running;

RETSIGTYPE
stop_server(int a) {
    keep_running = 0;
}

static struct MeterTable_entry *pMeterEntries=NULL;
static unsigned int MaxRegisteredEntry=0;

#if 0
/* This function might be useful during development for debugging purposes */
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

static const char * string_oid(oid o[], size_t l)
{
   size_t i;
   static char out[100];
   int len=0;

   for(i=0; i<l; i++)
   {
      if(len > 80)
	 len = 80;
      sprintf(&out[len], "%ld", o[i]);
      len=strlen(out);
      if(i<(l-1))
      {
         sprintf(&out[len], ".");
	 len=strlen(out);
      }
   }
   return out;
} /* string_oid */
#endif

static int oid_part_match(oid o1[], oid o2[], size_t l)
{
   size_t i;

   for(i=0; i<l; i++)
      if(o1[i] != o2[i])
         return 0;
   return 1;
}

static u_char *
agent_h_obis(struct variable *vp, oid *name, size_t *length, int exact,
    size_t *var_len, WriteMethod **write_method)
{
   static long long_ret;
   unsigned int index;
   unsigned int obis_index;
   struct obis_data *obis;
   int count=0;
      
   if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
      return NULL;
   if(*length < 8)
      return NULL;
   do
   {
      if(count++ > 3*MaxRegisteredEntry) /* this should not happen */
	 return NULL;
      index = name[*length -1];
      if(index < 1) return NULL;
      if(index > MaxRegisteredEntry) return NULL;
      obis_index = vp->magic;
      obis = &(pMeterEntries[index-1].ObisEntries[obis_index]);
      if(!oid_part_match(&name[*length -6], obis->obis_oid, 5))
	 if(header_simple_table(vp, name, length, exact, var_len,
				write_method, -1))
	    return NULL;
   } while((!exact) && !oid_part_match(&name[*length -6], obis->obis_oid, 5));
   if(oid_part_match(&name[*length -6], obis->obis_oid, 5))
   {
      switch(name[*length -7])
      {
	 case COLUMN_METEROBISDESCRIPTION:
	    if(!obis->description_len)
	    {
	       return NULL;
	    }
	    else
	    {
	       *var_len = obis->description_len;
	       return (u_char *) obis->description;
	    }
	 case COLUMN_METEROBISUNIT:
	    if(!obis->unit_len)
	    {
	       return NULL;
	    }
	    else
	    {
	       *var_len = obis->unit_len;
	       return (u_char *) obis->unit;
	    }
	 case COLUMN_METEROBISLATEST:
	    if(!obis->latest_is_valid)
	    {
	       return NULL;
	    }
	    else
	    {
	       long_ret = obis->latest_value;
	       return (u_char *) &long_ret;
	    }
	 case COLUMN_METEROBIS6MINMEAN:
	    if(!obis->mean6m_is_valid)
	    {
	       return NULL;
	    }
	    else
	    {
	       long_ret = obis->mean6m_value;
	       return (u_char *) &long_ret;
	    }
	 case COLUMN_METEROBIS6MINMAX:
	    if(!obis->max6m_is_valid)
	    {
	       return NULL;
	    }
	    else
	    {
	       long_ret = obis->max6m_value;
	       return (u_char *) &long_ret;
	    }
	 case COLUMN_METEROBIS6MINMIN:
	    if(!obis->min6m_is_valid)
	    {
	       return NULL;
	    }
	    else
	    {
	       long_ret = obis->min6m_value;
	       return (u_char *) &long_ret;
	    }
	 default:
	    break;
      }
   }
   return NULL;
} /* agent_h_obis */

static u_char *
agent_h_meter(struct variable *vp, oid *name, size_t *length, int exact,
    size_t *var_len, WriteMethod **write_method)
{
   static long long_ret;
   unsigned int index;
   struct MeterTable_entry *entry;

   if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
      return NULL;
   index = name[*length -1];
   /* fprintf(stderr, "index: %d\n", index); */
   if(index > MaxRegisteredEntry) return NULL;
   entry = &(pMeterEntries[index-1]);
   /* fprintf(stderr, "low enough\n", index); */
   
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

int
main (int argc, char **argv) {
  int background = 1; /* change if you not want to run in the background */
  int syslog = 1; /* change this if you not want to use syslog */
  char *conffile = ETC_DIR "/obis2snmp_config.json";
  int opt;
  struct json_object *conf_obj, *meter_array, *meter_obj;
  const char *driver;
  const char *parameters;
  int num_meters;
  int i, o;
  struct driver_data *drivers=NULL;
  char driver_path[256];
  time_t last_time_updated=0;
  time_t current_time;
  char descr[20];

  curl_global_init(CURL_GLOBAL_NOTHING);
  
  while ((opt = getopt(argc, argv, "vhc:")) != -1) {
     switch(opt) {
	case 'c':
	   conffile=optarg;
	   break;
	case 'h':
	case 'v':
	default:
	   fprintf(
	      stderr,
	      "obis2snmp_agentxd version %s\n", VERSION_STRING);
	   fprintf(
	      stderr,
	      "Copyright (c) Henrik Carlqvist, BSD-2-Clause license\n");
	   fprintf(stderr,
		   "Usage: %s [-c /path/to/config.json]\n"
		   "(-c defaults to %s)\n",
		   argv[0], conffile);
	   exit(EXIT_FAILURE);
     }
  }
  /* print log errors to syslog or stderr */
  netsnmp_enable_subagent();
  if (syslog)
    snmp_enable_calllog();
  else
    snmp_enable_stderrlog();

  conf_obj = json_object_from_file(conffile);

  if(!conf_obj) {
     snmp_log(LOG_CRIT,"Failed reading %s as json\n", conffile);
     fprintf(stderr,"Failed reading %s as json\n", conffile);
     exit(EXIT_FAILURE);
  }
  meter_array = json_object_object_get(conf_obj, "meters");
  if(!meter_array) {
     snmp_log(LOG_CRIT,"File %s does not have any meter array!\n", conffile);
     fprintf(stderr,"File %s does not have any meter array!\n", conffile);
     exit(EXIT_FAILURE);
  }
  num_meters = json_object_array_length(meter_array);
  MaxRegisteredEntry = num_meters;
  if(num_meters < 1) {
     snmp_log(LOG_CRIT,"File %s does not have any meters in array!\n",
	      conffile);
     fprintf(stderr,"File %s does not have any meters in array!\n",
	      conffile);
     exit(EXIT_FAILURE);
  }
  pMeterEntries = calloc(num_meters, sizeof(struct MeterTable_entry));
  drivers = calloc(num_meters, sizeof(struct driver_data));
  if((!drivers)||(!pMeterEntries)) {
     snmp_log(LOG_CRIT,"Calloc failed!\n");
     exit(EXIT_FAILURE);
  }
  /* initialize the agent library */
  init_agent("MeterTable");

  for(i=0; i<num_meters; i++){
     meter_obj = json_object_array_get_idx(meter_array, i);
     driver = json_object_get_string(
	json_object_object_get(meter_obj, "driver"));
     parameters = json_object_get_string(
	json_object_object_get(meter_obj, "parameters"));

     /* printf("Driver: '%s' , parameters: '%s'\n", driver, parameters); */
     snprintf(driver_path, 256, "%s.so", driver);
     /* printf("Trying to open: %s\n", driver_path); */
     drivers[i].dlhandle = dlopen(driver_path,  RTLD_NOW);
     if(!drivers[i].dlhandle)
     {
	snmp_log(LOG_CRIT,"driver %s load failure, %s\n",
		 driver_path, dlerror());
	fprintf(stderr, "driver %s load failure, %s\n",
		driver_path, dlerror());
     }
     else
     {
	drivers[i].init_driver = dlsym(drivers[i].dlhandle, "init_driver");
	if(drivers[i].init_driver)
	{
	   drivers[i].update_driver_data = dlsym(drivers[i].dlhandle,
						 "update_driver_data");
	   drivers[i].remove_driver = dlsym(drivers[i].dlhandle,
					    "remove_driver");
	   drivers[i].instance = drivers[i].init_driver(&pMeterEntries[i],
							parameters);
	   if(pMeterEntries[i].valid)
	   {
	      struct variable8 agent_meter_vars[6]= {
		 { COLUMN_METERINDEX, ASN_INTEGER, RONLY, agent_h_meter,
		   1, { COLUMN_METERINDEX } },
		 { COLUMN_METERTYPE, ASN_OCTET_STR, RONLY, agent_h_meter,
		   1, { COLUMN_METERTYPE } },
		 { COLUMN_METERIP, ASN_OCTET_STR, RONLY, agent_h_meter,
		   1, { COLUMN_METERIP } },
		 { COLUMN_METERMAC, ASN_OCTET_STR, RONLY, agent_h_meter,
		   1, { COLUMN_METERMAC } },
		 { COLUMN_METERRSSI, ASN_INTEGER, RONLY, agent_h_meter,
		   1, { COLUMN_METERRSSI } },
		 { COLUMN_METERMULTIPLIER, ASN_INTEGER, RONLY, agent_h_meter,
		   1, { COLUMN_METERMULTIPLIER } },
	      };
	      size_t num_vars = 1; /* We allways have an index */
	      oid       MeterTableEntry_oid[MeterTable_oid_len+1];

	      memcpy(MeterTableEntry_oid, MeterTable_oid,
		     MeterTable_oid_len*sizeof(oid));
	      MeterTableEntry_oid[MeterTable_oid_len] = 1;

	      if(pMeterEntries[i].MeterType_len)
		 num_vars++;
	      else
		 memmove(&agent_meter_vars[num_vars],
			 &agent_meter_vars[num_vars+1],
			 (5-num_vars)*sizeof(struct variable8));
	      if(pMeterEntries[i].MeterIP_len)
		 num_vars++;
	      else
		 memmove(&agent_meter_vars[num_vars],
			 &agent_meter_vars[num_vars+1],
			 (5-num_vars)*sizeof(struct variable8));
	      if(pMeterEntries[i].MeterMAC_len)
		 num_vars++;
	      else
		 memmove(&agent_meter_vars[num_vars],
			 &agent_meter_vars[num_vars+1],
			 (5-num_vars)*sizeof(struct variable8));
	      if(pMeterEntries[i].MeterRSSI)
		 num_vars++;
	      else
		 memmove(&agent_meter_vars[num_vars],
			 &agent_meter_vars[num_vars+1],
			 (5-num_vars)*sizeof(struct variable8));
	      num_vars++; /* Multiplier should allways be valid */
	      snprintf(descr, 19, "Meter%d", i);
	      if (register_mib_range(descr,
				     (struct variable *) agent_meter_vars,
				     sizeof(struct variable8),
				     num_vars,
				     MeterTableEntry_oid,
				     sizeof(MeterTableEntry_oid)/sizeof(oid),
				     DEFAULT_MIB_PRIORITY,
				     i+1,
				     i+2,
				     NULL) !=
		  MIB_REGISTERED_OK)
	      {
		 DEBUGMSGTL(("register_mib", "%s registration failed\n",
			     descr));
	      }
	      for(o=0; o < pMeterEntries[i].numObisEntries; o++)
	      {
		 oid oid_name[6][MAX_OID_LEN];
		 int j,r;
		 pMeterEntries[i].ObisEntries[o].description_len =
		    strlen(pMeterEntries[i].ObisEntries[o].description);
		 pMeterEntries[i].ObisEntries[o].unit_len =
		    strlen(pMeterEntries[i].ObisEntries[o].unit);
		 for(r=0;r<6;r++)
		 {
		    oid_name[r][0] = r+COLUMN_METEROBISDESCRIPTION;
		    for(j=0;j<5;j++)
		    {
		       oid_name[r][j+1] =
			  pMeterEntries[i].ObisEntries[o].obis_oid[j];
		    }
		 }
		 {
		    struct variable8 agent_obis_vars[6]= {
		       { o, ASN_OCTET_STR, RONLY, agent_h_obis, 6,
			 {oid_name[0][0],oid_name[0][1],oid_name[0][2],
			  oid_name[0][3],oid_name[0][4],oid_name[0][5]} },
		       { o, ASN_OCTET_STR, RONLY, agent_h_obis, 6,
			 {oid_name[1][0],oid_name[1][1],oid_name[1][2],
			  oid_name[1][3],oid_name[1][4],oid_name[1][5]} },
		       { o, ASN_INTEGER,   RONLY, agent_h_obis, 6,
			 {oid_name[2][0],oid_name[2][1],oid_name[2][2],
			  oid_name[2][3],oid_name[2][4],oid_name[2][5]} },
		       { o, ASN_INTEGER,   RONLY, agent_h_obis, 6,
			 {oid_name[3][0],oid_name[3][1],oid_name[3][2],
			  oid_name[3][3],oid_name[3][4],oid_name[3][5]} },
		       { o, ASN_INTEGER,   RONLY, agent_h_obis, 6,
			 {oid_name[4][0],oid_name[4][1],oid_name[4][2],
			  oid_name[4][3],oid_name[4][4],oid_name[4][5]} },
		       { o, ASN_INTEGER,   RONLY, agent_h_obis, 6,
			 {oid_name[5][0],oid_name[5][1],oid_name[5][2],
			  oid_name[5][3],oid_name[5][4],oid_name[5][5]} },
		    };
		    num_vars = 2; /* We allways have description and unit */
		    if(pMeterEntries[i].ObisEntries[o].latest_is_valid)
		       num_vars++;
		    else
		       memmove(&agent_obis_vars[num_vars],
			       &agent_obis_vars[num_vars+1],
			       (5-num_vars)*sizeof(struct variable8));
		    if(pMeterEntries[i].ObisEntries[o].mean6m_is_valid)
		       num_vars++;
		    else
		       memmove(&agent_obis_vars[num_vars],
			       &agent_obis_vars[num_vars+1],
			       (5-num_vars)*sizeof(struct variable8));
		    if(pMeterEntries[i].ObisEntries[o].max6m_is_valid)
		       num_vars++;
		    else
		       memmove(&agent_obis_vars[num_vars],
			       &agent_obis_vars[num_vars+1],
			       (5-num_vars)*sizeof(struct variable8));
		    if(pMeterEntries[i].ObisEntries[o].min6m_is_valid)
		       num_vars++;
		    else
		       memmove(&agent_obis_vars[num_vars],
			       &agent_obis_vars[num_vars+1],
			       (5-num_vars)*sizeof(struct variable8));
#if 1
		    if (register_mib_range(descr,
					   (struct variable *)agent_obis_vars,
					   sizeof(struct variable8),
					   num_vars,
					   MeterTableEntry_oid,
					   sizeof(MeterTableEntry_oid) /
					     sizeof(oid),
					   DEFAULT_MIB_PRIORITY,
					   i+1,
					   i+2,
					   NULL) !=
#endif
#if 0
		    if (register_mib(descr,
				     (struct variable *)agent_obis_vars,
				     sizeof(struct variable8),
				     num_vars,
				     MeterTableEntry_oid,
				     sizeof(MeterTableEntry_oid) /
				     sizeof(oid)) !=
#endif
			MIB_REGISTERED_OK)
		    {
		       DEBUGMSGTL(("register_mib", "%s registration failed\n",
				   descr));
		    }
		 }
	      }
	   }
	}
	else
	{
	   snmp_log(LOG_CRIT,
		    "driver %s is missing init_driver function!\n",
		    driver);
	   drivers[i].remove_driver = NULL;
	}
     }
  }

  json_object_put(conf_obj); /* free json stuff */
  
  /* make us a agentx client. */
  netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_AGENT_ROLE, 1);

  /* run in background, if requested */
  if (background && netsnmp_daemonize(1, !syslog))
      exit(1);

  /* initialize tcpip, if necessary */
  SOCK_STARTUP;

  /* mib code: */
  /* init_MeterTable();  */

  /* example-demon will be used to read example-demon.conf files. */
  init_snmp("MeterTable");

  /* In case we recevie a request to stop (kill -TERM or kill -INT) */
  keep_running = 1;
  signal(SIGTERM, stop_server);
  signal(SIGINT, stop_server);

  snmp_log(LOG_INFO,"MeterTable-daemon is up and running.\n");

  /* your main loop here... */
  while(keep_running) {
    /* if you use select(), see snmp_select_info() in snmp_api(3) */
    /*     --- OR ---  */
     i=agent_check_and_process(1); /* 0 == don't block */
     time(&current_time);
     if((!i) || ((current_time - last_time_updated) > 10))
     {
	last_time_updated = current_time;
	for(i=0; i<num_meters;i++)
	   if(drivers[i].update_driver_data)
	      drivers[i].update_driver_data(drivers[i].instance,
					    &pMeterEntries[i]);
     }
  }
  for(i=0; i<num_meters; i++){
     if(drivers[i].remove_driver)
	drivers[i].remove_driver(drivers[i].instance, &pMeterEntries[i]);
  }
  /* at shutdown time */
  snmp_shutdown("MeterTable");
  /* shutdown_MeterTable(); */
  SOCK_CLEANUP;
  curl_global_cleanup();

  free(drivers);
  free(pMeterEntries);
  return 0;
}

