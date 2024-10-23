#!/bin/ash

# This script might be useful as inspiration on how to use the template
# with mrtg

set -e

if [ $# -ne 1 ]; then
  echo "Usage: $0 <hostname>"
  echo "This script creates the directory obis and populates it with"
  echo "mrtg files. The host must have snmpd running and the obis2snmpd"
  echo "daemon configured running."
else
mkdir -p `dirname $0`/obis
cd `dirname $0`/obis
ln -sf ../meter.png folder.png
cd -

/opt/bin/cfgmaker --global "WorkDir: `dirname $(pwd)/$0`/obis" \
                               --global 'Options[_]: bits,growright' \
                               --host-template=obis2snmp.htp \
			       --nointerfaces \
                               public@${1}:::::2 > `dirname $0`/obis/mrtg.cfg
fi

