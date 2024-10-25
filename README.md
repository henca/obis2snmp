# obis2snmp
SNMP agentx proxy providing obis data from utility meters

## Drivers
At the time of this writing, only one driver has been implemented, for the
[P1IB HAN port device](https://remne.tech/p1ib/).
Contributions of more drivers are welcome!

## Configuration
The configuration file (default /usr/local/etc/obis2snmp_config.json) lists
drives to be used together with their parameters:

`{"meters": [<br>
   {"driver": "P1IB", "parameters": "ip=192.168.67.112"},<br>
   {"driver": "P1IB", "parameters": "ip=192.168.67.112,multiplier=1"}<br>
 ]}`

In the example above I really only have one utility meter to read, but
make it appear as two meters by giving slightly different parameters.

## Parameters for different drivers
### P1IB
|Parameter |Mandatory|Explanation                              |
|----------|---------|-----------------------------------------|
|ip        |yes      |The IP address of he P1IB unit to monitor|
|multiplier|no       |(default 1000) The value to multiply the obis floating point values with to get enough precision in SNMP integer values.|

## License
The source code for the program has a BSD-2-Clause license and the MIB file
describing the OIDs used has a Zlib license as described in [LICENSE](LICENSE)

## Building and installation
Once downloaded and unpacked or checked out from git, in the directory
with this README.md there is a Makefile.

That Makefile is used to build:
`make`

The Makefile is also used to install:
`sudo make install`

During build time as well as install time a number of variables can be set. If
any of those variables are customized the same variable should be given at
build time and install time.

PREFIX (default /usr/local), where to install, the same PREFIX should be used
       at build time and install time.

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

The **json-c** library is also needed to read the configuration file and for the
P1IB driver.

To get data from meters **libcurl** is also needed.

## Note
This project is work in progress in pre-beta state.

