############################################################
## Flow API library available at or after DPDK v20.02
############################################################

ifeq ($(shell [ -n $(RTE_VER_YEAR) ] && [ $(RTE_VER_YEAR) -ge 20 ] && [ -n $(HAVE_FLOW_API) ] && echo true),true)

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
	cp -u $(RTE_SDK)/app/test-pmd/cmdline.c $(PARSE_PATH)
	# Strip the main function off to prevent complilation errors, while linking with Click
	sed -i '$$!N;/main(int/,$$d' $(PARSE_PATH)/testpmd.c
	sed -i 's/\([*(>]\)template\([= .,[;)]\)/\1ptemplate\2/g' $(PARSE_PATH)/config.c $(PARSE_PATH)/testpmd.h
	sed -i 's/rte_os_shim/rte_os/' $(PARSE_PATH)/testpmd.h
	sed -i 's/} template;/} ptemplate;/' $(PARSE_PATH)/testpmd.h
	sed -i 's/static cmdline_parse_ctx_t/cmdline_parse_ctx_t/' $(PARSE_PATH)/cmdline.c
	head -n -1 $(PARSE_PATH)/testpmd.c > $(PARSE_PATH)/testpmd_t.c;
	mv $(PARSE_PATH)/testpmd_t.c $(PARSE_PATH)/testpmd.c
	# Strip off testpmd report messages as our library prints its own report messages
	sed -i '/printf("Flow rule #\%u created\\n", pf->id);/d' $(PARSE_PATH)/config.c
	sed -i '/printf("Flow rule #\%u destroyed\\n", pf->id);/d' $(PARSE_PATH)/config.c
ifeq ($(shell [ -n "$(RTE_VER_YEAR)" ] && ( ( [ "$(RTE_VER_YEAR)" -ge 22 ] && [ "$(RTE_VER_MONTH)" -ge 11 ] ) || [ $(RTE_VER_YEAR) -ge 23 ] ) && echo true),true)
	cp -u $(RTE_SDK)/drivers/net/mlx5/mlx5_testpmd.h $(PARSE_PATH)
endif
	touch $(PARSE_PATH)/.sentinel

test-pmd/%.o: ${PARSE_PATH}.sentinel
	cp -u $(RTE_SDK)/app/test-pmd/$*.c $(PARSE_PATH)
	$(CC) -o $@ -O3 -c $(PARSE_PATH)/$*.c $(CFLAGS) -I$(RTE_SDK)/app/test-pmd/

# Object files present across all DPDK versions
PARSE_OBJS = \
	test-pmd/cmdline_flow.o \
	test-pmd/macfwd.o test-pmd/cmdline.o test-pmd/txonly.o test-pmd/csumonly.o test-pmd/flowgen.o \
	test-pmd/icmpecho.o test-pmd/ieee1588fwd.o test-pmd/iofwd.o test-pmd/macswap.o \
	test-pmd/rxonly.o \

# Additional object files
PARSE_OBJS += test-pmd/cmdline_mtr.o test-pmd/cmdline_tm.o
PARSE_OBJS += test-pmd/bpf_cmd.o
PARSE_OBJS += test-pmd/parameters.o
ifeq ($(shell [ -n "$(RTE_VER_YEAR)" ] && ( [ "$(RTE_VER_YEAR)" -eq 20 ] && [ "$(RTE_VER_MONTH)" -le 05 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/softnicfwd.o
endif
PARSE_OBJS += test-pmd/noisy_vnf.o test-pmd/util.o
ifeq ($(shell [ -n "$(RTE_VER_YEAR)" ] && ( ( [ "$(RTE_VER_YEAR)" -ge 20 ] && [ "$(RTE_VER_MONTH)" -ge 08 ] ) || [ $(RTE_VER_YEAR) -ge 21 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/5tswap.o
endif
ifeq ($(shell [ -n "$(RTE_VER_YEAR)" ] && ( ( [ "$(RTE_VER_YEAR)" -ge 21 ] && [ "$(RTE_VER_MONTH)" -ge 11 ] ) || [ $(RTE_VER_YEAR) -ge 22 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/shared_rxq_fwd.o test-pmd/cmd_flex_item.o
endif
ifeq ($(shell [ -n "$(RTE_VER_YEAR)" ] && ( ( [ "$(RTE_VER_YEAR)" -ge 22 ] && [ "$(RTE_VER_MONTH)" -gt 11 ] ) || [ $(RTE_VER_YEAR) -ge 23 ] ) && echo true),true)
	PARSE_OBJS += test-pmd/cmdline_cman.o
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

endif # RTE_VERSION >= 20.02
