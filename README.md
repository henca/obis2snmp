# obis2snmp
SNMP agentx proxy providing OBIS data from utility meters

## Building and installation
Once downloaded and unpacked or checked out from git, in the directory
with this README.md there is a Makefile.

That Makefile is used to build:
`make`

The Makefile is also used to install:
`sudo make install`

During build time as well as install time a number of variables can be set. If
any of those variables are customized the same variable should be given both at
build time and install time.

PREFIX (default /usr/local), where to install.

SBIN_DIR (default PREFIX/sbin) where the binary obis2snmp_agentxd is installed

ETC_DIR (default PREFIX/etc or /etc if PREFIX=/usr) where obis2snmp_config.json
        is installed.

DESTDIR (default empty) Top level directory which might be useful when building
        a package. Please be aware though that the MIB file probably will
        require some extra tinkering when building such a package.

Next to SBIN_DIR a directory lib or lib64 will be created with a subdirectory
obis2snmp containing driver plugins for different meters.

## Prerequisites
This agentx daemon of course depends upon **net-snmp**

The **json-c** library is also needed to read the configuration file and for
the drivers.

To get data from meters **libcurl** is also needed.

## Drivers
At the time of this writing, only two drivers have been implemented, for the
[P1IB HAN port device](https://remne.tech/p1ib/) and the
[WiMBIB Wireless M-Bus Interface Bridge](https://remne.tech/wimbib/) .
Contributions of more drivers are welcome!

## Configuration
The configuration file (default /usr/local/etc/obis2snmp_config.json) lists
drives to be used together with their parameters:

`{"meters": [`  
`   {"driver": "P1IB", "parameters": "ip=192.168.67.112"},`  
`   {"driver": "WiMBIB", "parameters": "ip=192.168.67.115,showextra=true"},`  
` ]}`

In the example above I really only have one utility meter to read, but
make it appear as two meters by giving slightly different parameters.

## Parameters for different drivers
### P1IB
|Parameter |Mandatory|Explanation                              |
|----------|---------|-----------------------------------------|
|ip        |yes      |The IP address of he P1IB unit to monitor|
|multiplier|no       |(default 1000) The value to multiply the OBIS floating point values with to get enough precision in SNMP integer values. For P1IB it is really not recommended to change the default value.|

### WiMBIB
|Parameter |Mandatory|Explanation                                |
|----------|---------|-------------------------------------------|
|ip        |yes      |The IP address of he WiMBIB unit to monitor|
|showextra |no       |(default false) Whether to show extra data (temperatures and statuses) for which there are no standard OBIS values. The WiMBIB does not really provide its data as OBIS values, but some of its data (volumes) have standard OBIS codes used by other meters. With this value set to false, only those volumes with standard OBIS codes will be presented. With this value set to true, also temperatures and statuses will be presented using made up non standard OBIS codes translated to SNMP oids .|

## Configuration of net-snmp
In the file `snmpd.conf` which usually is below /etc/snmp you should add the
following line to present these OBIS values:

`view    systemview    included   .1.3.6.1.4.1.62368.1.1`

You should also make sure that net-snmp has enabled support for agentx
subagents with the following line in `snmpd.conf`:

`master agentx`

Once the net-snmp snmpd daemon has been restarted with the above settings in
`snmpd.conf` the `obis2snmp_agentxd` can be started. The best way to start
`obis2snmp_agentxd` is from some startup script like `rc.local` depending upon
your distribution.

The program obis2snmp_agentxd does not require root privileges to fetch data
from drivers, but it might need root privileges to connect to the net-snmp
daemon depending on your settings in `snmpd.conf`. Before changing any such
setting you should read up on the drawbacks of running a process as root
versus the drawbacks of allowing non root processes to provide snmp data to
net-snmp.

## License
The source code for the program has a BSD-2-Clause license and the MIB file
describing the OIDs used has a Zlib license as described in [LICENSE](LICENSE)

## Screenshots
### The help text
![help text](screenshots/help.png?raw=true "Help text")

### snmpwalk
![snmpwalk](screenshots/snmpwalk.png?raw=true "snmpwalk")

### mrtg
The output from snmpwalk contains plenty of data, but it becomes really
useful when you use your favorite tool to present snmp data in graphs. The
graphs below are from [mrtg](https://oss.oetiker.ch/mrtg/)
![mrtg](screenshots/seamonkey.png?raw=true "mrtg")

