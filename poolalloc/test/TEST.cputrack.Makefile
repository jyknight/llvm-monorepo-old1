##===- poolalloc/TEST.cputrack.Makefile --------------------*- Makefile -*-===##
#
# Makefile for measuring cache miss rates with the Solaris cputrack utility
#
##===----------------------------------------------------------------------===##

TESTNAME = $*
CURDIR  := $(shell cd .; pwd)
PROGDIR := $(shell cd $(LLVM_OBJ_ROOT)/projects/llvm-test; pwd)/
RELDIR  := $(subst $(PROGDIR),,$(CURDIR))


#
# Solaris cputrack command that will sample the counters every 500 ticks.
# All programs must complete in under 500 ticks (500 seconds by default),
# and the counters must not wrap around
#
CPUTRACK       := cputrack -T 500

#
# Events for the Ultrasparc IIIi processor.
#
# EVENTS_DC_RD:
#   Read accesses and misses on the L1 Data Cache.
#
# EVENTS_DC_WR:
#   Write accesses and misses on the L1 Data Cache.  This probably isn't as
#   valuable as it sounds because the cache is write through, non allocate.
#   So, as I understand it, the L2 and Write Caches are where the write action
#   is.
#
# EVENTS_EC_MISS:
#   Read misses and overall misses for the L2 Cache (aka the Embedded Cache).
#
EVENTS_DC_RD   := -c pic0=DC_rd,pic1=DC_rd_miss
EVENTS_DC_WR   := -c pic0=DC_wr,pic1=DC_wr_miss
EVENTS_EC_MISS := -c pic0=EC_rd_miss,pic1=EC_misses
EVENTS_TLB     := -c pic0=EC_rd_miss,pic1=DTLB_miss
EVENTS_WC      := -c pic0=Cycle_cnt,pic1=WC_miss

#
# Once the results are generated, create files containing each individiual
# piece of performance information.
#

#
# Generate events for Pool Allocated CBE
#
$(PROGRAMS_TO_TEST:%=Output/test.$(TEST).pa.%): \
Output/test.$(TEST).pa.%: Output/%.poolalloc.cbe Output/test.$(TEST).%
	@echo "========================================="
	@echo "Running '$(TEST)' test on '$(TESTNAME)' program"
ifeq ($(RUN_OPTIONS),)
	$(VERB) cat $(STDIN_FILENAME) | $(CPUTRACK) $(EVENTS_DC_RD) -o $@ $<
else
	$(VERB) cat $(STDIN_FILENAME) | $(CPUTRACK) $(EVENTS_DC_RD) -o $@ $< $(RUN_OPTIONS)
endif

#
# Generate events for CBE
#
$(PROGRAMS_TO_TEST:%=Output/test.$(TEST).%): \
Output/test.$(TEST).%: Output/%.cbe
	@echo "========================================="
	@echo "Running '$(TEST)' test on '$(TESTNAME)' program"
ifeq ($(RUN_OPTIONS),)
	$(VERB) cat $(STDIN_FILENAME) | $(CPUTRACK) $(EVENTS_DC_RD) -o $@ $<
else
	$(VERB) cat $(STDIN_FILENAME) | $(CPUTRACK) $(EVENTS_DC_RD) -o $@ $< $(RUN_OPTIONS)
endif

$(PROGRAMS_TO_TEST:%=Output/%.$(TEST).report.txt): \
Output/%.$(TEST).report.txt: Output/test.$(TEST).pa.% Output/test.$(TEST).%
	touch $@

$(PROGRAMS_TO_TEST:%=test.$(TEST).%): \
test.$(TEST).%: Output/%.$(TEST).report.txt
	@echo "---------------------------------------------------------------"
	@echo ">>> ========= '$(RELDIR)/$*' Program"
	@echo "---------------------------------------------------------------"
	@cat $<

