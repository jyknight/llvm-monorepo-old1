##===- TEST.dsgraph.Makefile -------------------------------*- Makefile -*-===##
#
# This recursively traverses the programs, computing DSGraphs for each of the
# programs in the testsuite.
#
##===----------------------------------------------------------------------===##

RELDIR  := $(subst $(PROJ_OBJ_ROOT),,$(PROJ_OBJ_DIR))

# We require the programs to be linked with libdummy
#include $(LEVEL)/Makefile.dummylib

# Pathname to poolalloc object tree
PADIR   := $(LLVM_OBJ_ROOT)/projects/poolalloc

# Bits of runtime to improve analysis
PA_PRE_RT := $(PADIR)/$(CONFIGURATION)/lib/libpa_pre_rt.bca

# Pathame to the DSA pass dynamic library
DSA_SO   := $(PADIR)/$(CONFIGURATION)/lib/libLLVMDataStructure$(SHLIBEXT)
ASSIST_SO := $(PADIR)/$(CONFIGURATION)/lib/libAssistDS$(SHLIBEXT)

# Command for running the opt program
RUNOPT := $(RUNTOOLSAFELY) $(LLVM_OBJ_ROOT)/projects/poolalloc/$(CONFIGURATION)/bin/watchdog $(LOPT) -load $(DSA_SO)

# PASS - The dsgraph pass to run: ds, bu, td
PASS := td

ANALYZE_OPTS := -stats -time-passes -disable-output -dsstats
#ANALYZE_OPTS := -stats -time-passes -dsstats 
ANALYZE_OPTS +=  -instcount -disable-verify 
MEM := -track-memory -time-passes -disable-output

SAFE_OPTS := -internalize -scalarrepl -deadargelim -globaldce -basiccg -inline 

$(PROGRAMS_TO_TEST:%=Output/%.linked1.bc): \
Output/%.linked1.bc: Output/%.linked.rbc $(LOPT)
	-$(RUNOPT) -disable-opt $(SAFE_OPTS) -info-output-file=$(CURDIR)/$@.info -stats -time-passes $< -f -o $@ 

$(PROGRAMS_TO_TEST:%=Output/%.llvm1.bc): \
Output/%.llvm1.bc: Output/%.linked1.bc $(LLVM_LDDPROG)
	-$(RUNTOOLSAFELY) $(LLVMLD) -disable-opt $(SAFE_OPTS) -info-output-file=$(CURDIR)/$@.info -stats -time-passes  $(LLVMLD_FLAGS) $< -lc $(LIBS) -o Output/$*.llvm1

$(PROGRAMS_TO_TEST:%=Output/%.temp1.bc): \
Output/%.temp1.bc: Output/%.llvm1.bc 
	-$(RUNTOOLSAFELY) $(LLVMLD) -disable-opt $(SAFE_OPTS) -link-as-library $< $(PA_PRE_RT) -o $@

$(PROGRAMS_TO_TEST:%=Output/%.opt1.bc): \
Output/%.opt1.bc: Output/%.llvm1.bc $(LOPT) $(ASSIST_SO)
	-$(RUNOPT) -load $(ASSIST_SO) -disable-opt -info-output-file=$(CURDIR)/$@.info -instnamer -internalize -varargsfunc -indclone -funcspec -ipsccp -deadargelim  -simplifygep -die -mergegep -mergearrgep -die -globaldce -simplifycfg -deadargelim -arg-simplify -varargsfunc -indclone -funcspec -deadargelim -globaldce -die -simplifycfg -gep-args -deadargelim -die -mergegep -die -globaldce -stats -time-passes $< -f -o $@ 

$(PROGRAMS_TO_TEST:%=Output/%.opt.bc): \
Output/%.opt.bc: Output/%.llvm1.bc $(LOPT) $(ASSIST_SO)
	-$(RUNOPT) -load $(ASSIST_SO) -disable-opt -info-output-file=$(CURDIR)/$@.info -instnamer -internalize -varargsfunc -indclone -funcspec -ipsccp -deadargelim  -simplifygep -die -mergegep -die -mergearrgep -die -globaldce -simplifycfg -deadargelim -arg-simplify -die -varargsfunc -die -simplifycfg -globaldce -indclone -funcspec -deadargelim -globaldce -die -simplifycfg -gep-args -deadargelim -die -mergegep -die -mergearrgep -die -globaldce -stats -time-passes $< -f -o $@ 

$(PROGRAMS_TO_TEST:%=Output/%.temp2.bc): \
Output/%.temp2.bc: Output/%.temp1.bc $(LOPT) $(ASSIST_SO)
	-$(RUNOPT) -load $(ASSIST_SO) -disable-opt -info-output-file=$(CURDIR)/$@.info -instnamer -internalize  -varargsfunc -indclone -funcspec -ipsccp -deadargelim  -mergegep -die -globaldce -stats -time-passes $< -f -o $@ 

$(PROGRAMS_TO_TEST:%=Output/%.opt.s): \
Output/%.opt.s: Output/%.opt.bc $(LLC)
	-$(LLC) -f $< -o $@
$(PROGRAMS_TO_TEST:%=Output/%.llvm1.s): \
Output/%.llvm1.s: Output/%.llvm1.bc $(LLC)
	-$(LLC) -f $< -o $@

$(PROGRAMS_TO_TEST:%=Output/%.opt): \
Output/%.opt: Output/%.opt.s 
	-$(CC) $(CFLAGS) $<  $(LLCLIBS) $(LDFLAGS) -o $@
$(PROGRAMS_TO_TEST:%=Output/%.llvm1): \
Output/%.llvm1: Output/%.llvm1.s 
	-$(CC) $(CFLAGS) $<  $(LLCLIBS) $(LDFLAGS) -o $@

ifndef PROGRAMS_HAVE_CUSTOM_RUN_RULES

$(PROGRAMS_TO_TEST:%=Output/%.opt.out): \
Output/%.opt.out: Output/%.opt
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $< $(RUN_OPTIONS)
$(PROGRAMS_TO_TEST:%=Output/%.llvm1.out): \
Output/%.llvm1.out: Output/%.llvm1
	-$(RUNSAFELY) $(STDIN_FILENAME) $@ $< $(RUN_OPTIONS)

else
$(PROGRAMS_TO_TEST:%=Output/%.opt.out): \
Output/%.opt.out: Output/%.opt
	-$(SPEC_SANDBOX) opt-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/opt-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/opt-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time
$(PROGRAMS_TO_TEST:%=Output/%.llvm1.out): \
Output/%.llvm1.out: Output/%.llvm1
	-$(SPEC_SANDBOX) llvm1-$(RUN_TYPE) $@ $(REF_IN_DIR) \
             $(RUNSAFELY) $(STDIN_FILENAME) $(STDOUT_FILENAME) \
                  ../../$< $(RUN_OPTIONS)
	-(cd Output/opt-$(RUN_TYPE); cat $(LOCAL_OUTPUTS)) > $@
	-cp Output/opt-$(RUN_TYPE)/$(STDOUT_FILENAME).time $@.time

endif

$(PROGRAMS_TO_TEST:%=Output/%.opt.diff-nat): \
Output/%.opt.diff-nat: Output/%.out-nat Output/%.opt.out
	@cp Output/$*.out-nat Output/$*.opt.out-nat
	-$(DIFFPROG) nat $*.opt $(HIDEDIFF)

$(PROGRAMS_TO_TEST:%=Output/%.llvm1.diff-nat): \
Output/%.llvm1.diff-nat: Output/%.out-nat Output/%.llvm1.out
	@cp Output/$*.out-nat Output/$*.llvm1.out-nat
	-$(DIFFPROG) nat $*.opt $(HIDEDIFF)


$(PROGRAMS_TO_TEST:%=Output/%.$(TEST).report.txt): \
Output/%.$(TEST).report.txt: Output/%.opt.bc Output/%.LOC.txt $(LOPT) Output/%.out-nat Output/%.opt.diff-nat Output/%.llvm1.diff-nat
	@# Gather data
	-($(RUNOPT) -dsa-$(PASS) -enable-type-inference-opts -dsa-stdlib-no-fold $(ANALYZE_OPTS) $<)> $@.time.1 2>&1
	-($(RUNOPT) -dsa-$(PASS) $(ANALYZE_OPTS) $<)> $@.time.2 2>&1
	@# Emit data.
	@echo "---------------------------------------------------------------" > $@
	@echo ">>> ========= '$(RELDIR)/$*' Program" >> $@
	@echo "---------------------------------------------------------------" >> $@
	@/bin/echo -n "LOC: " >> $@
	@cat Output/$*.LOC.txt >> $@
	@echo >> $@
	@/bin/echo -n "MEMINSTS: " >> $@
	-@grep 'Number of memory instructions' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "FOLDEDNODES: " >> $@
	-@grep 'Number of nodes completely folded' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "TOTALNODES: " >> $@
	-@grep 'Number of nodes allocated' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "MAXGRAPHSIZE: " >> $@
	-@grep 'Maximum graph size' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "GLOBALSGRAPH: " >> $@
	-@grep 'td.GlobalsGraph.dot' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "SCCSIZE: " >> $@
	-@grep 'Maximum SCC Size in Call Graph' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES TYPED_O: " >> $@
	-@grep 'Number of loads/stores which are fully typed' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES UNTYPED_O: " >> $@
	-@grep 'Number of loads/stores which are untyped' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES TYPED: " >> $@
	-@grep 'Number of loads/stores which are fully typed' $@.time.2 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES UNTYPED: " >> $@
	-@grep 'Number of loads/stores which are untyped' $@.time.2 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES TYPED1: " >> $@
	-@grep 'Number of loads/stores which are access a DSNode with 1 type' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES TYPED2: " >> $@
	-@grep 'Number of loads/stores which are access a DSNode with 2 type' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES TYPED3: " >> $@
	-@grep 'Number of loads/stores which are access a DSNode with 3 type' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES TYPED4: " >> $@
	-@grep 'Number of loads/stores which are access a DSNode with >3 type' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES I: " >> $@
	-@grep 'Number of loads/stores which are on incomplete nodes' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES E: " >> $@
	-@grep 'Number of loads/stores which are on external nodes' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "ACCESSES U: " >> $@
	-@grep 'Number of loads/stores which are on unknown nodes' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "STD_LIB_FOLD: " >> $@
	-@grep 'Number of nodes folded in std lib' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "I2PB: " >> $@
	-@grep 'Number of inttoptr used only in cmp' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "I2PS: " >> $@
	-@grep 'Number of inttoptr from ptrtoint' $@.time.1 >> $@
	@echo >> $@
	@# Emit timing data.
	@/bin/echo -n "TIME: " >> $@
	-@grep '  Local Data Structure' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "TIME: " >> $@
	-@grep '  Bottom-up Data Structure' $@.time.1 >> $@
	@echo >> $@
	@/bin/echo -n "TIME: " >> $@
	-@grep '  Top-down Data Structure' $@.time.1 >> $@
	@echo >> $@
	@# Emit AssistDS stats
	@/bin/echo -n "CLONED_FUNCSPEC: " >> $@
	-@grep 'Number of Functions Cloned in FuncSpec' $<.info >> $@
	@echo >> $@
	@/bin/echo -n "CLONED_INDCLONE: " >> $@
	-@grep 'Number of Functions Cloned in IndClone' $<.info >> $@
	@echo >> $@
	@/bin/echo -n "VARARGS_CALLS: " >> $@
	-@grep 'Number of Calls Simplified' $<.info >> $@
	@echo >> $@
	@/bin/echo -n "ARG_SMPL: " >> $@
	-@grep 'Number of Args changeable' $<.info >> $@
	@echo >> $@
	@/bin/echo -n "CALLS1: " >> $@
	-@grep 'Number of calls that could not be resolved' $@.time.1 >> $@
	@echo >> $@
	@-if test -f Output/$*.opt.diff-nat; then \
	  printf "RUN: 1" >> $@;\
        fi


$(PROGRAMS_TO_TEST:%=test.$(TEST).%): \
test.$(TEST).%: Output/%.$(TEST).report.txt
	@echo "---------------------------------------------------------------"
	@echo ">>> ========= '$(RELDIR)/$*' Program"
	@echo "---------------------------------------------------------------"
	@cat $<

# Define REPORT_DEPENDENCIES so that the report is regenerated if analyze or
# dummylib is updated.
#
REPORT_DEPENDENCIES := $(DUMMYLIB) $(LOPT)

