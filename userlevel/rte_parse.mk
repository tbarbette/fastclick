############################################################
## Flow API library available at or after DPDK v17.05
############################################################

ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -ge 17 ] && [ $(RTE_VER_MONTH) -ge 05 ] ) || [ $(RTE_VER_YEAR) -ge 18 ] ) && [ -n $(HAVE_FLOW_API) ] && echo true),true)

$(debug LIBRTE_PARSE=YES)

RTE_VERSION=$(RTE_VER_YEAR).$(RTE_VER_MONTH).$(RTE_VER_MINOR)
PARSE_PATH=../lib/librte_parse_$(RTE_VERSION)/

${PARSE_PATH}.sentinel:
	# Create the necessary folders for librte_parse
	mkdir -p $(PARSE_PATH)
	mkdir -p test-pmd
	# Copy testpmd.c of the target DPDK version
	cp -u $(RTE_SDK)/app/test-pmd/testpmd.c $(PARSE_PATH)
	cp -u $(RTE_SDK)/app/test-pmd/testpmd.h $(PARSE_PATH)
	cp -u $(RTE_SDK)/app/test-pmd/config.c $(PARSE_PATH)
	# Strip the main function off to prevent complilation errors, while linking with Click
	sed -i '/main(int/Q' $(PARSE_PATH)/testpmd.c
	head -n -1 $(PARSE_PATH)/testpmd.c > $(PARSE_PATH)/testpmd_t.c;
	mv $(PARSE_PATH)/testpmd_t.c $(PARSE_PATH)/testpmd.c
	# Strip off testpmd report messages as our library prints its own report messages
	sed -i '/printf("Flow rule #\%u created\\n", pf->id);/d' $(PARSE_PATH)/config.c
	sed -i '/printf("Flow rule #\%u destroyed\\n", pf->id);/d' $(PARSE_PATH)/config.c
# Until DPDK 17.11 these structs need to be declared as extern in testpmd.h and be defined in testpmd.c. Our patch in DPDK 18.02 solves this issue :)
ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 17 ] ) ) && echo true),true)
	# Deal with .h
	sed -i 's/^uint8_t\sport_numa\[RTE_MAX_ETHPORTS\]\;/extern uint8_t port_numa[RTE_MAX_ETHPORTS];/g' $(PARSE_PATH)/testpmd.h
	sed -i 's/^uint8_t\srxring_numa\[RTE_MAX_ETHPORTS\]\;/extern uint8_t rxring_numa[RTE_MAX_ETHPORTS];/g' $(PARSE_PATH)/testpmd.h
	sed -i 's/^uint8_t\stxring_numa\[RTE_MAX_ETHPORTS\]\;/extern uint8_t txring_numa[RTE_MAX_ETHPORTS];/g' $(PARSE_PATH)/testpmd.h
	sed -i '/extern\senum\sdcb_queue_mapping_mode\sdcb_q_mapping\;/d' $(PARSE_PATH)/testpmd.h
	# Deal with .c
	sed -i '120i uint8_t port_numa[RTE_MAX_ETHPORTS];' $(PARSE_PATH)/testpmd.c
	sed -i '121i uint8_t rxring_numa[RTE_MAX_ETHPORTS];' $(PARSE_PATH)/testpmd.c
	sed -i '122i uint8_t txring_numa[RTE_MAX_ETHPORTS];' $(PARSE_PATH)/testpmd.c
endif
# Between DPDK 18.08 and 19.08 these structs need to be declared as external in testpmd.h. DPDK 19.11 solves this issue :)
ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 18 ] && [ $(RTE_VER_MONTH) -ge 08 ] ) || ( [ $(RTE_VER_YEAR) -eq 19 ] && [ $(RTE_VER_MONTH) -le 08 ] ) ) && echo true),true)
	$(info "needed")
	sed -i 's/struct\snvgre_encap_conf\snvgre_encap_conf;/extern struct nvgre_encap_conf nvgre_encap_conf;/g' $(PARSE_PATH)/testpmd.h
	sed -i 's/struct\svxlan_encap_conf\svxlan_encap_conf;/extern struct vxlan_encap_conf vxlan_encap_conf;/g' $(PARSE_PATH)/testpmd.h
endif
# Between DPDK 18.11 and 19.08 these structs need to be declared as external in testpmd.h. DPDK 19.11 solves this issue :)
ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 18 ] && [ $(RTE_VER_MONTH) -ge 11 ] ) || ( [ $(RTE_VER_YEAR) -eq 19 ] && [ $(RTE_VER_MONTH) -le 08 ] ) ) && echo true),true)
	# Deal with .h
	sed -i 's/struct\sl2_decap_conf\sl2_decap_conf;/extern struct l2_decap_conf l2_decap_conf;/g' $(PARSE_PATH)/testpmd.h
	sed -i 's/struct\sl2_encap_conf\sl2_encap_conf;/extern struct l2_encap_conf l2_encap_conf;/g' $(PARSE_PATH)/testpmd.h
	sed -i 's/struct\smplsogre_decap_conf\smplsogre_decap_conf;/extern struct mplsogre_decap_conf mplsogre_decap_conf;/g' $(PARSE_PATH)/testpmd.h
	sed -i 's/struct\smplsogre_encap_conf\smplsogre_encap_conf;/extern struct mplsogre_encap_conf mplsogre_encap_conf;/g' $(PARSE_PATH)/testpmd.h
	sed -i 's/struct\smplsoudp_decap_conf\smplsoudp_decap_conf;/extern struct mplsoudp_decap_conf mplsoudp_decap_conf;/g' $(PARSE_PATH)/testpmd.h
	sed -i 's/struct\smplsoudp_encap_conf\smplsoudp_encap_conf;/extern struct mplsoudp_encap_conf mplsoudp_encap_conf;/g' $(PARSE_PATH)/testpmd.h
	# Deal with .c
	sed -i '133i struct l2_decap_conf l2_decap_conf;' $(PARSE_PATH)/testpmd.c
	sed -i '134i struct l2_encap_conf l2_encap_conf;' $(PARSE_PATH)/testpmd.c
	sed -i '135i struct mplsogre_decap_conf mplsogre_decap_conf;' $(PARSE_PATH)/testpmd.c
	sed -i '136i struct mplsogre_encap_conf mplsogre_encap_conf;' $(PARSE_PATH)/testpmd.c
	sed -i '137i struct mplsoudp_decap_conf mplsoudp_decap_conf;' $(PARSE_PATH)/testpmd.c
	sed -i '138i struct mplsoudp_encap_conf mplsoudp_encap_conf;' $(PARSE_PATH)/testpmd.c
endif
	touch $(PARSE_PATH)/.sentinel

ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 19 ] && [ $(RTE_VER_MONTH) -le 08 ] ) || [ $(RTE_VER_YEAR) -lt 19 ] ) && echo true),true)
.NOTPARALLEL: ${PARSE_PATH}.sentinel
endif

test-pmd/%.o: ${PARSE_PATH}.sentinel
	cp -u $(RTE_SDK)/app/test-pmd/$*.c $(PARSE_PATH)
	$(CC) -o $@ -O3 -c $(PARSE_PATH)/$*.c $(CFLAGS) -I$(RTE_SDK)/app/test-pmd/

# Object files present across all DPDK versions
PARSE_OBJS = \
	test-pmd/cmdline_flow.o \
	test-pmd/macfwd.o test-pmd/cmdline.o test-pmd/txonly.o test-pmd/csumonly.o test-pmd/flowgen.o \
	test-pmd/icmpecho.o test-pmd/ieee1588fwd.o test-pmd/iofwd.o test-pmd/macswap.o \
	test-pmd/rxonly.o \

# Additional object files, present at or after DPDK v17.11 but not at or after 18.08
ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 17 ] && [ $(RTE_VER_MONTH) -ge 11 ] ) || ( [ $(RTE_VER_YEAR) -eq 18 ] && [ $(RTE_VER_MONTH) -lt 08 ] ) ) && echo true),true)
	PARSE_OBJS += test-pmd/tm.o
endif

# Additional object files, present at or after DPDK v17.11
ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 17 ] && [ $(RTE_VER_MONTH) -ge 11 ] ) || [ $(RTE_VER_YEAR) -ge 18 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/cmdline_mtr.o test-pmd/cmdline_tm.o
endif

# Additional object files, present at or after DPDK v18.05
ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 18 ] && [ $(RTE_VER_MONTH) -ge 05 ] ) || [ $(RTE_VER_YEAR) -ge 19 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/bpf_cmd.o
endif

# Additional object files, present at or after DPDK v18.08
ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 18 ] && [ $(RTE_VER_MONTH) -ge 08 ] ) || [ $(RTE_VER_YEAR) -ge 19 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/parameters.o
	# Some sources are only available for limited DPDK versions
	ifeq ($(shell [ -n "$(RTE_VER_YEAR)" ] && ( [ $(RTE_VER_YEAR) -le 19 ] ) || ( [ "$(RTE_VER_YEAR)" -eq 20 ] && [ "$(RTE_VER_MONTH)" -le 05 ] ) && echo true),true)
		PARSE_OBJS += test-pmd/softnicfwd.o
	endif
endif

# Additional object files, present at or after DPDK v18.11
ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && ( ( [ $(RTE_VER_YEAR) -eq 18 ] && [ $(RTE_VER_MONTH) -ge 11 ] ) || [ $(RTE_VER_YEAR) -ge 19 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/noisy_vnf.o test-pmd/util.o
endif

# Additional object files, present at or after DPDK v20.08
ifeq ($(shell [ -n "$(RTE_VER_YEAR)" ] && ( ( [ "$(RTE_VER_YEAR)" -ge 20 ] && [ "$(RTE_VER_MONTH)" -ge 08 ] ) || [ $(RTE_VER_YEAR) -ge 21 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/5tswap.o
endif

CFLAGS += -I../lib/librte_parse_$(RTE_VERSION)
CXXFLAGS += -I../lib/librte_parse_$(RTE_VERSION)

${PARSE_PATH}/%.o: ${PARSE_PATH}.sentinel
	$(CC) -o $@ -O3 -c $(PARSE_PATH)/$*.c $(CFLAGS) -I$(RTE_SDK)/app/test-pmd/

librte_parse.a: $(PARSE_OBJS) $(PARSE_PATH)/testpmd.o $(PARSE_PATH)/config.o
	$(call verbose_cmd,$(AR_CREATE) librte_parse.a $(PARSE_OBJS) $(PARSE_PATH)/testpmd.o $(PARSE_PATH)/config.o,AR librte_parse.a)
	$(call verbose_cmd,$(RANLIB),RANLIB,librte_parse.a)

else

$(debug LIBRTE_PARSE=NO)

endif # RTE_VERSION >= 17.05
