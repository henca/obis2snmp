
NETSNMP_CONFIG = net-snmp-config
NETSNMP_CFLAGS := $(shell $(NETSNMP_CONFIG) --base-cflags)
NETSNMP_LIBS := $(shell $(NETSNMP_CONFIG) --agent-libs)
NETSNMP_LIB = $(shell $(NETSNMP_CONFIG) --libdir | awk -F / '{print $$NF}')

NETSNMP_PREFIX = $(shell $(NETSNMP_CONFIG) --prefix)
NETSNMP_MIBS_DIR = $(NETSNMP_PREFIX)/share/snmp/mibs
NETSNMP_LIBS_DIR = $(NETSNMP_PREFIX)/$(NETSNMP_LIB)/snmp/dlmod
PREFIX ?= /usr/local
SBIN_DIR ?= $(PREFIX)/sbin
ifeq ($(PREFIX),/usr)
ETC_DIR = /etc
else
ETC_DIR = $(PREFIX)/etc
endif

INSTALLED_CONFIG_FILE = $(ETC_DIR)/obis2snmp_config.json
SRC_CONFIG_FILE = etc/obis2snmp_config.json

INCDIR = inc
SRCDIR = src
OBJDIR = obj
BINDIR = bin
LIBDIR = $(notdir $(shell $(NETSNMP_CONFIG) --libdir))
PLGDIR = $(LIBDIR)/obis2snmp

PLGSRCDIR = plugin_src
PLGOBJDIR = plugin_obj

PLG_SRC_FILES=$(wildcard $(PLGSRCDIR)/*.c)
PLG_OBJ_FILES = $(PLG_SRC_FILES:$(PLGSRCDIR)/%.c=$(PLGOBJDIR)/%.o)
PLG_FILES = $(PLG_OBJ_FILES:$(PLGOBJDIR)/%.o=$(PLGDIR)/%.so)

AGENTX = $(BINDIR)/obis2snmp_agentxd

# some json-c versions deprecated useful functions which then was undeprecated
CFLAGS += -O2 `pkg-config --cflags json-c` -Wno-deprecated-declarations \
          `curl-config --cflags` \
          $(NETSNMP_CFLAGS) -fPIC -Wall -Wstrict-prototypes -I $(INCDIR) \
          -D ETC_DIR=\"$(ETC_DIR)\"
LDFLAGS += $(NETSNMP_LIBS) \
           `pkg-config --libs json-c` \
           `curl-config --libs` \
           -Wl,-rpath,'$$ORIGIN'/../$(PLGDIR) 

#OBJS = nvCtrlTable.o nvCtrlTable_data_access.o nvCtrlTable_data_get.o nvCtrlTable_interface.o

all: $(AGENTX) $(PLG_FILES)


SRC_FILES = $(wildcard $(SRCDIR)/*.c)
OBJ_FILES = $(SRC_FILES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

$(OBJ_FILES): $(OBJDIR)/%.o : $(SRCDIR)/%.c $(INC_FILES) Makefile | $(OBJDIR)
	gcc -c $(CFLAGS) -o $@ $<

$(OBJDIR) $(BINDIR) $(PLGOBJDIR) $(PLGDIR):
	mkdir -p $@


$(AGENTX): $(OBJ_FILES) | $(BINDIR)
	$(CC) -o $@ $^ $(LDFLAGS)

$(PLG_FILES): $(PLGDIR)/%.so: $(PLGOBJDIR)/%.o | $(PLGDIR)
	gcc -shared -o $@ $<

$(PLGOBJDIR)/%.o: $(PLGSRCDIR)/%.c | $(PLGOBJDIR)
	gcc -c $(CFLAGS) -o $@ $<

clean:
	rm -rf $(OBJS) agentx-daemon.o $(AGENTX) $(PLGOBJDIR)

install: $(AGENTX) | $(INSTALLED_CONFIG_FILE)
	install -d $(DESTDIR)$(NETSNMP_MIBS_DIR)
	install -m 644 HenrikC-MIB.txt $(DESTDIR)$(NETSNMP_MIBS_DIR)
	install -d $(DESTDIR)$(SBIN_DIR)
	install -m 755 -s $(AGENTX) $(DESTDIR)$(SBIN_DIR)
	install -d $(DESTDIR)$(SBIN_DIR)/../$(PLGDIR)
	install -m 755 $(PLG_FILES) $(DESTDIR)$(SBIN_DIR)/../$(PLGDIR)
	grep HenrikC-MIB $(DESTDIR)$(NETSNMP_MIBS_DIR)/../snmp.conf || \
           echo "mibs +HenrikC-MIB" >> \
           $(DESTDIR)$(NETSNMP_MIBS_DIR)/../snmp.conf

# Avoid overwriting customized config file by making this rule order-only
$(INSTALLED_CONFIG_FILE): | $(SRC_CONFIG_FILE)
	install -d $(ETC_DIR)
	install -b -m 644 $| $(@D)
