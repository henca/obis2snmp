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
#include <curl/curl.h>

#include "driver.h"

struct driver_data {
   void *dlhandle;
   void *instance;
   void *(*init_driver)(long, const char *);
   void (*remove_driver)(void *);
};

static int keep_running;

RETSIGTYPE
stop_server(int a) {
    keep_running = 0;
}

int
main (int argc, char **argv) {
  int background = 0; /* change this if you not want to run in the background */
  int syslog = 1; /* change this if you not want to use syslog */
  char *conffile = "/etc/config.json";
  int opt;
  struct json_object *conf_obj, *meter_array, *meter_obj;
  const char *driver;
  const char *parameters;
  int num_meters;
  int i;
  struct driver_data *drivers=NULL;
  char driver_path[256];

  curl_global_init(CURL_GLOBAL_NOTHING);
  
  while ((opt = getopt(argc, argv, "c:")) != -1) {
     switch(opt) {
	case 'c':
	   conffile=optarg;
	   break;
	default:
	   fprintf(stderr, "Usage: %s [-c /path/to/config.json]\n", argv[0]);
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
     exit(EXIT_FAILURE);
  }
  meter_array = json_object_object_get(conf_obj, "meters");
  if(!meter_array) {
     snmp_log(LOG_CRIT,"File %s does not have any meter array!\n", conffile);
     exit(EXIT_FAILURE);
  }
  num_meters = json_object_array_length(meter_array);
  if(num_meters < 1) {
     snmp_log(LOG_CRIT,"File %s does not have any meters in array!\n",
	      conffile);
     exit(EXIT_FAILURE);
  }
  drivers = calloc(num_meters, sizeof(struct driver_data));
  /* initialize the agent library */
  init_agent("MeterTable");

  for(i=0; i<num_meters; i++){
     meter_obj = json_object_array_get_idx(meter_array, i);
     driver = json_object_get_string(
	json_object_object_get(meter_obj, "driver"));
     parameters = json_object_get_string(
	json_object_object_get(meter_obj, "parameters"));

     printf("Driver: '%s' , parameters: '%s'\n", driver, parameters);
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
	   drivers[i].remove_driver = dlsym(drivers[i].dlhandle,
					    "remove_driver");
	   drivers[i].instance = drivers[i].init_driver(i+1, parameters);
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
    agent_check_and_process(1); /* 0 == don't block */
  }
  for(i=0; i<num_meters; i++){
     if(drivers[i].remove_driver)
	drivers[i].remove_driver(drivers[i].instance);
  }
  /* at shutdown time */
  snmp_shutdown("MeterTable");
  /* shutdown_MeterTable(); */
  SOCK_CLEANUP;
  curl_global_cleanup();

  return 0;
}

