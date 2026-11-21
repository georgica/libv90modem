# libv90modem: build, test, install.
#
#   make                 static and shared library, pkg-config file, examples (in $(BUILD))
#   make check           the simulator suite on the host
#   make check-arm       the same, cross-built (ARM_CC) and run under qemu-arm,
#                        PAR (8) programs at a time (tools/par-run.sh)
#   make check-ubsan     the host suite with undefined behaviour trapped (clang)
#   make install         into $(DESTDIR)$(PREFIX); make uninstall takes it out
#   make examples        examples/answer_file (answering a recording), examples/call_loop
#                        (calling the library's own answerer; make check runs it)
#   make crd-scan        K56flex's CRd detector over recorded calls (RECS=<dir>): no detection
#   make bench-v90       record a simulated V.90 call, replay it on the host and
#                        under qemu, through the internals and the public API
#   make bench-x2        the same for x2 (a call of the simulated client); cost-x2 counts
#   make bench-v90a      the same for the V.90 analogue modem's downstream receiver
#   make bench-v90c      the same for the whole V.90 calling modem (orig, v90a)
#   make bench-v92c      ... and for it in V.92 (PCM upstream); cost-v92c counts
#   make prof-v90c       per tick and per function ARM instruction counts (prof-v92c, PPM=)
#   make bench-bell      the calling modem in Bell 103 and Bell 212A; cost-bell counts
#   make test-interop    against spandsp (SPANDSP=its install prefix), host only
#   make test-x2-courier an emulated USR Courier calls our answerer (COURIER_EMU=courier-emu
#                        checkout, PAD=0|3|6, MODS=x2, STRICT_RENEG=1; tools/x2-peer/README.md),
#                        host only
#   make test-k56-sm56   Motorola's SM56 calls our answerer (DMODEM=D-Modem checkout, built;
#                        MODS=k56|none, RBS=<hex>, DOWN=<bps>, STRICT_RENEG=1;
#                        tools/k56-peer/README.md), host only, needs gcc -m32
#
# Cross-compiling: set CC (and BUILD to keep the objects apart), e.g.
#   make CC=arm-linux-gcc BUILD=build/arm
# ar, ld and objcopy are taken from the same prefix as CC.
#
# Signal-path code is portable C99, fixed-point. The simulator (sim/) is a
# test peer and may use double; it is never installed.

NAME     := libv90modem
VERSION  := 0.4.0
SOVER    := 0

CC       ?= cc
CFLAGS   ?= -std=c99 -O2 -Wall -Wextra -Werror
# -D options for the modem code (bench experiments, trace builds)
EXTRA_CPPFLAGS ?=
CPPFLAGS_ALL := -D_POSIX_C_SOURCE=200809L $(EXTRA_CPPFLAGS)
LDLIBS   ?= -lm
BUILD    ?= build

# binutils next to the compiler (arm-linux-gcc -> arm-linux-ar ...) when there
# are any; musl-gcc, clang and cc use the host's
CROSS    := $(if $(filter %-gcc,$(notdir $(CC))),$(patsubst %gcc,%,$(CC)),)
CROSS    := $(if $(CROSS),$(if $(shell command -v $(CROSS)ld),$(CROSS)),)
AR       := $(CROSS)ar
LD       := $(CROSS)ld
OBJCOPY  := $(CROSS)objcopy
NM       := $(CROSS)nm

PREFIX   ?= /usr/local
LIBDIR   ?= $(PREFIX)/lib
INCDIR   ?= $(PREFIX)/include
BINDIR   ?= $(PREFIX)/bin
PCDIR    ?= $(LIBDIR)/pkgconfig

ARM_CC   ?= $(firstword $(wildcard $(CURDIR)/../toolchain/*/bin/arm-linux-gcc) arm-linux-gcc)
QEMU     ?= qemu-arm-static
SPANDSP  ?= $(CURDIR)/../peers/spandsp

INC      := -Iinclude -Iapi -Imodem -Iv90 -Iv92 -Iv34 -Iv32 -Iv42 -Isim

# ---- the library ----
CORE_SRC := $(wildcard modem/*.c) $(wildcard v90/*.c) $(wildcard v34/*.c) $(wildcard v32/*.c) $(wildcard v92/*.c) \
            $(addprefix v42/,v42_hdlc.c v42_detect.c v42_comp.c v44_comp.c v42_lapm.c v42.c)
API_SRC  := api/v90modem.c
LIB_SRC  := $(API_SRC) $(CORE_SRC)
LIB_HDR  := include/v90modem.h api/v90modem_priv.h $(wildcard modem/*.h v90/*.h v34/*.h v32/*.h v92/*.h v42/*.h)
LIB_OBJ  := $(patsubst %.c,$(BUILD)/obj/%.o,$(LIB_SRC))
PIC_OBJ  := $(patsubst %.c,$(BUILD)/pic/%.o,$(LIB_SRC))

STATIC   := $(BUILD)/$(NAME).a
SHARED   := $(BUILD)/$(NAME).so.$(VERSION)
PCFILE   := $(BUILD)/$(NAME).pc
# never installed: every internal symbol visible, plus the simulator; for the
# test suite, the tools here, and for programs that test against the
# simulated callers (v90d's call tests)
DEV      := $(BUILD)/$(NAME)-dev.a

# ---- the simulator and the test suite ----
SIM_SRC  := sim/g711.c sim/noise.c sim/filt.c sim/resamp.c sim/channel.c sim/v8call.c sim/callpeer.c sim/p2a.c sim/qammod.c \
            sim/p34a.c v90/test/v90_dec.c sim/p2c.c sim/v34peer.c sim/v32call.c sim/v32line.c sim/v92chan.c sim/v92a.c sim/v92call.c sim/dschan.c sim/x2call.c sim/k56call.c
SIM_OBJ  := $(patsubst %.c,$(BUILD)/obj/%.o,$(SIM_SRC))
MODEM_SRC := modem/v21.c modem/ansam.c modem/v8msg.c modem/async.c modem/v8rx.c modem/v8ans.c \
             modem/scramble.c modem/v22tx.c modem/v22rx.c modem/v22bis.c modem/v14.c modem/answer.c \
             modem/info.c modem/dpsk.c modem/probe.c modem/p2d.c modem/qamrx.c modem/ec.c modem/ecdly.c modem/tonedet.c modem/v34train.c modem/p34d.c modem/pcmenc.c \
             modem/v34mp.c modem/p2ans.c modem/v34mod.c modem/v34dx.c modem/v34ans.c modem/anstone.c modem/v8orig.c modem/orig.c modem/p2call.c modem/v34orig.c modem/v90arx.c modem/v90adec.c modem/v90dil.c modem/v90a.c modem/pcmtx.c modem/v92aseq.c modem/belltone.c \
             modem/x2rec.c modem/x2map.c modem/x2tab.c modem/x2bank.c modem/x2tx.c modem/x2d.c modem/k56tab.c modem/k56enc.c modem/v8bismsg.c modem/v8bis.c \
             $(wildcard v90/*.c) $(wildcard v34/*.c) $(wildcard v32/*.c) $(wildcard v92/*.c)
T_SRC    := $(SIM_SRC) $(MODEM_SRC)
HDRS     := $(wildcard modem/*.h sim/*.h test/*.h v90/*.h v34/*.h v92/*.h v90/test/v90_dec.h)
T_CFLAGS := $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(EXTRA_CPPFLAGS) -Iv90 -Imodem -Isim -Iv92 -Iv34
TESTS    := test_g711 test_noise test_filt test_resamp test_channel test_v21 test_ansam test_v8msg test_v8rx test_v8ans \
            test_v22tx test_v22rx test_v22bis test_v14 test_answer test_info test_dpsk test_probe test_p2 test_qamrx test_ec test_ecdly test_v34train test_sres test_answer_v90 test_p34 test_pcmenc \
            test_v34mp test_p2ans test_v34mod test_v34dx test_v34call test_answer_v34 test_answer_v32 test_answer_v90_v34 \
            test_p2_v92 test_v92call test_answer_v92 test_v92real test_answer_lin \
            test_anstone test_v8orig test_orig test_orig_v32 test_p2call test_orig_v34 test_v90arx test_v90arx_robust test_v90adec test_v90dil test_p2v90 test_v90a test_orig_v90 test_orig_v90_te test_orig_seeds test_orig_retrain test_pcmtx test_v92aseq test_p2v92a test_v92a test_orig_v92 test_belltone test_orig_bell test_realcheck test_x2_pad test_answer_x2 test_x2rec test_x2map test_x2tx test_k56tab test_k56enc test_v8bismsg test_v8bis test_answer_k56
# the public API, against the library as it is installed
API_TESTS := test_api
SUBS     := v34 v90 v92 v42 v32

.PHONY: test-x2-courier test-k56-sm56 test-fixtures test-examples all lib dev install uninstall clean check check-arm check-ubsan test test-arm test-api test-interop examples bench-v90 bench-v34 bench-v90a bench-v90c bench-v92c replay
all: lib examples
lib: $(STATIC) $(SHARED) $(PCFILE)
dev: $(DEV)

$(BUILD)/obj/%.o: %.c $(LIB_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS_ALL) -fvisibility=hidden $(INC) -c -o $@ $<
$(BUILD)/pic/%.o: %.c $(LIB_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS_ALL) -fvisibility=hidden -fPIC $(INC) -c -o $@ $<

# Static: one relocatable object in which everything but the public API is
# renamed v90m__i_* (tools/symmap.sh), so that the internals cannot clash
# with a program's symbols. Renamed, not made local: some targets' code
# cannot reach a local symbol the way it reaches a global one.
$(STATIC): $(LIB_OBJ) tools/symmap.sh
	$(LD) -r -o $(BUILD)/$(NAME).o $(LIB_OBJ)
	tools/symmap.sh $(NM) $(BUILD)/$(NAME).o > $(BUILD)/$(NAME).map
	$(OBJCOPY) --redefine-syms=$(BUILD)/$(NAME).map $(BUILD)/$(NAME).o
	rm -f $@ && $(AR) rcs $@ $(BUILD)/$(NAME).o
# what the static library defines: the public API, its internals renamed,
# and the compiler's own helpers; nothing else
.PHONY: check-symbols
check-symbols: $(STATIC)
	@bad=$$($(NM) -g --defined-only $(STATIC) | awk 'NF >= 3 { print $$3 }' | sed 's/^_v90m_/v90m_/' \
	      | grep -vE '^(v90m_|__|\.)' || true); \
	 if [ -n "$$bad" ]; then echo "$(STATIC) defines symbols outside v90m_*:"; echo "$$bad" | head; exit 1; fi; \
	 echo "symbols: $(STATIC) defines only v90m_* ($$($(NM) -g --defined-only $(STATIC) | grep -c ' _*v90m__i_') internal)"
$(SHARED): $(PIC_OBJ)
	$(CC) -shared -Wl,-soname,$(NAME).so.$(SOVER) -o $@ $(PIC_OBJ) $(LDLIBS)
	ln -sf $(NAME).so.$(VERSION) $(BUILD)/$(NAME).so.$(SOVER)
	ln -sf $(NAME).so.$(SOVER) $(BUILD)/$(NAME).so
# the .pc file names the install paths: made again when they change
PC_VARS := $(PREFIX)|$(LIBDIR)|$(INCDIR)|$(VERSION)
.PHONY: .pc-force
$(BUILD)/pc.vars: .pc-force
	@mkdir -p $(BUILD); echo '$(PC_VARS)' | cmp -s - $@ || echo '$(PC_VARS)' > $@
$(PCFILE): $(NAME).pc.in Makefile $(BUILD)/pc.vars
	@mkdir -p $(BUILD)
	sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@LIBDIR@|$(LIBDIR)|' -e 's|@INCDIR@|$(INCDIR)|' -e 's|@VERSION@|$(VERSION)|' $< > $@
$(DEV): $(LIB_OBJ) $(SIM_OBJ)
	rm -f $@ && $(AR) rcs $@ $(LIB_OBJ) $(SIM_OBJ)

install: lib $(BUILD)/v90m-replay
	install -d $(DESTDIR)$(INCDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(PCDIR) $(DESTDIR)$(BINDIR)
	install -m 644 include/v90modem.h $(DESTDIR)$(INCDIR)/
	install -m 644 $(STATIC) $(DESTDIR)$(LIBDIR)/
	install -m 755 $(SHARED) $(DESTDIR)$(LIBDIR)/
	ln -sf $(NAME).so.$(VERSION) $(DESTDIR)$(LIBDIR)/$(NAME).so.$(SOVER)
	ln -sf $(NAME).so.$(SOVER) $(DESTDIR)$(LIBDIR)/$(NAME).so
	install -m 644 $(PCFILE) $(DESTDIR)$(PCDIR)/
	install -m 755 $(BUILD)/v90m-replay $(DESTDIR)$(BINDIR)/
uninstall:
	rm -f $(DESTDIR)$(INCDIR)/v90modem.h $(DESTDIR)$(LIBDIR)/$(NAME).a $(DESTDIR)$(LIBDIR)/$(NAME).so \
	      $(DESTDIR)$(LIBDIR)/$(NAME).so.$(SOVER) $(DESTDIR)$(LIBDIR)/$(NAME).so.$(VERSION) \
	      $(DESTDIR)$(PCDIR)/$(NAME).pc $(DESTDIR)$(BINDIR)/v90m-replay

# ---- tests: each test_X links test/test_X.c with the sources, as always ----
$(BUILD)/test_%: test/test_%.c $(T_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ $< $(T_SRC) $(LDLIBS)
$(BUILD)/arm_test_%: test/test_%.c $(T_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(ARM_CC) $(T_CFLAGS) -static -o $@ $< $(T_SRC) $(LDLIBS)
# x2's pad octets: a host tool (tools/x2_pad.c), not in the library; its
# test links the tool and the G.711 codec alone
X2PAD_SRC := tools/x2_pad.c sim/g711.c
$(BUILD)/test_x2_pad: test/test_x2_pad.c $(X2PAD_SRC) tools/x2_pad.h sim/g711.h test/test.h
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ $< $(X2PAD_SRC) $(LDLIBS)
$(BUILD)/arm_test_x2_pad: test/test_x2_pad.c $(X2PAD_SRC) tools/x2_pad.h sim/g711.h test/test.h
	@mkdir -p $(BUILD)
	$(ARM_CC) $(T_CFLAGS) -static -o $@ $< $(X2PAD_SRC) $(LDLIBS)
# the mapper's test checks modem/x2tab's pad octets against the tool's rule
$(BUILD)/test_x2map: test/test_x2map.c $(T_SRC) tools/x2_pad.c tools/x2_pad.h $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ $< $(T_SRC) tools/x2_pad.c $(LDLIBS)
$(BUILD)/arm_test_x2map: test/test_x2map.c $(T_SRC) tools/x2_pad.c tools/x2_pad.h $(HDRS)
	@mkdir -p $(BUILD)
	$(ARM_CC) $(T_CFLAGS) -static -o $@ $< $(T_SRC) tools/x2_pad.c $(LDLIBS)
# The API test: the answering modem from the static library as installed
# (only v90m_* visible), the simulated caller from the dev archive. Both
# carry the modem's code; the static library's copy is local to it.
API_INC := -Iinclude -Itest -Isim -Imodem -Iv90 -Iv92 -Iv34
$(BUILD)/api_test_%: test/api/test_%.c $(STATIC) $(DEV) include/v90modem.h
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(API_INC) -o $@ $< $(STATIC) $(DEV) $(LDLIBS)
$(BUILD)/arm/api_test_%: test/api/test_%.c include/v90modem.h
	$(MAKE) -s BUILD=$(BUILD)/arm CC=$(ARM_CC) $(BUILD)/arm/$(NAME).a $(BUILD)/arm/$(NAME)-dev.a check-symbols
	$(ARM_CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(API_INC) -static -o $@ $< $(BUILD)/arm/$(NAME).a $(BUILD)/arm/$(NAME)-dev.a $(LDLIBS)

test: $(addprefix $(BUILD)/,$(TESTS))
	@echo "== running host tests =="
	@fail=0; for t in $(TESTS); do $(abspath $(BUILD))/$$t || fail=1; done; \
	 if [ $$fail -eq 0 ]; then echo "ALL TESTS PASS"; else echo "TESTS FAILED"; exit 1; fi
test-arm: $(addprefix $(BUILD)/arm_,$(TESTS))
	@echo "== running ARM tests under $(QEMU) =="
	@if tools/par-run.sh $(abspath $(BUILD))/arm_log "$(QEMU)" "[arm]" $(addprefix $(abspath $(BUILD))/arm_,$(TESTS)); \
	 then echo "ALL ARM TESTS PASS"; else echo "ARM TESTS FAILED"; exit 1; fi
test-api: $(addprefix $(BUILD)/api_,$(API_TESTS))
	@fail=0; for t in $(API_TESTS); do $(abspath $(BUILD))/api_$$t || fail=1; done; [ $$fail -eq 0 ]
test-api-arm: $(addprefix $(BUILD)/arm/api_,$(API_TESTS))
	@fail=0; for t in $(API_TESTS); do $(QEMU) $(abspath $(BUILD))/arm/api_$$t || fail=1; done; [ $$fail -eq 0 ]

check: test test-api test-examples test-fixtures check-symbols
	@for d in $(SUBS); do echo "== $$d =="; $(MAKE) -s -C $$d BUILD=build test || exit 1; done; echo "ALL HOST CHECKS PASS"
# recordings of real calls (test/fixtures/fixtures.txt) through the replay tool
test-fixtures: $(BUILD)/v90m-replay
	@echo "== real-modem recordings =="; test/fixtures/check.sh $(abspath $(BUILD))/v90m-replay
# the subdirectories build in their own build/ unless BUILD names another place
SUB_BUILD = $(if $(filter build,$(BUILD)),build,$(abspath $(BUILD))/$$d)
check-arm: test-arm test-api-arm
	@for d in $(SUBS); do echo "== $$d (arm) =="; $(MAKE) -s -C $$d BUILD=$(SUB_BUILD) test-arm ARM_CC=$(ARM_CC) QEMU="$(QEMU)" || exit 1; done; echo "ALL ARM CHECKS PASS"
# The host suite with undefined behaviour trapped (clang's -fsanitize=undefined):
# a signed overflow or a shift of a negative number is what lets the same C
# compute differently on another target or compiler; any of them fails.
UBSAN_CC    ?= clang
UBSAN_FLAGS := -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined
check-ubsan:
	@$(MAKE) -s BUILD=$(BUILD)/ubsan CC=$(UBSAN_CC) CFLAGS="$(UBSAN_FLAGS)" test test-api
	@for d in $(SUBS); do echo "== $$d (ubsan) =="; $(MAKE) -s -C $$d BUILD=$(abspath $(BUILD))/ubsan/$$d CC=$(UBSAN_CC) CFLAGS="$(UBSAN_FLAGS)" test || exit 1; done; echo "ALL UBSAN CHECKS PASS"

INTEROP := interop_v8_spandsp interop_v22bis_spandsp interop_v8orig_spandsp interop_orig_v22bis_spandsp
$(BUILD)/interop_%: test/interop_%.c test/peer_spandsp.c $(T_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -Wno-unused-parameter -I$(SPANDSP)/include -o $@ $< test/peer_spandsp.c $(T_SRC) -L$(SPANDSP)/lib -Wl,-rpath,$(SPANDSP)/lib -lspandsp -ltiff $(LDLIBS)
test-interop: $(addprefix $(BUILD)/,$(INTEROP))
	@echo "== running interop tests =="
	@fail=0; for t in $(INTEROP); do $(abspath $(BUILD))/$$t || fail=1; done; \
	 if [ $$fail -eq 0 ]; then echo "ALL INTEROP TESTS PASS"; else echo "INTEROP TESTS FAILED"; exit 1; fi

# ---- the examples, through the public API only: answer a recorded call;
# call the library's own answerer (run by make check) ----
EXAMPLES := answer_file call_loop
examples: $(addprefix $(BUILD)/,$(EXAMPLES))
$(addprefix $(BUILD)/,$(EXAMPLES)): $(BUILD)/%: examples/%.c $(STATIC) include/v90modem.h
	$(CC) $(CFLAGS) -Iinclude -o $@ $< $(STATIC) $(LDLIBS)
test-examples: $(BUILD)/call_loop
	@echo "== examples/call_loop =="; $(abspath $(BUILD))/call_loop

# ---- the replay tool (ships with the library): modem and V.42 only, built
# from the sources with the trace hooks in ----
REPLAY_SRC := tools/v90m_replay.c tools/trace.c $(LIB_SRC)
$(BUILD)/v90m-replay: $(REPLAY_SRC) $(LIB_HDR) tools/trace.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS_ALL) -DP34D_TRACE $(INC) -Itools -o $@ $(REPLAY_SRC) $(LDLIBS)
replay: $(BUILD)/v90m-replay
# K56flex's CRd detector over what the answerer heard in recorded calls
# (tools/k56_crd_scan.c): RECS=<dir> of v90m-replay recordings (*.raw);
# a detection is a false CRd. Host only, not part of check.
RECS ?=
$(BUILD)/k56-crd-scan: tools/k56_crd_scan.c modem/v8bis.c modem/v8bismsg.c modem/v21.c v90/v90_pcm.c $(LIB_HDR)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ tools/k56_crd_scan.c modem/v8bis.c modem/v8bismsg.c modem/v21.c v90/v90_pcm.c $(LDLIBS)
.PHONY: crd-scan
crd-scan: $(BUILD)/k56-crd-scan
	@if [ -z "$(RECS)" ]; then echo "RECS not set (a directory of recordings)"; exit 1; fi; \
	$(abspath $(BUILD))/k56-crd-scan $(RECS)/*.raw | tail -1
# our K56flex encoder's octets and data bits at every rate, pad and robbed
# mask (tools/k56enc_dump.c), for SM56's decoder harness outside the
# repository (docs/history/k56-facts.md "Oracle"). Host only, not part of check.
K56ENC_DUMP_SRC := tools/k56enc_dump.c modem/k56enc.c modem/k56tab.c v34/v34_shell.c v90/v90_pcm.c v90/v90_scr.c
$(BUILD)/k56enc-dump: $(K56ENC_DUMP_SRC) $(LIB_HDR)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ $(K56ENC_DUMP_SRC) $(LDLIBS)
.PHONY: k56-dump
k56-dump: $(BUILD)/k56enc-dump
	@mkdir -p $(BUILD)/k56dump
	$(abspath $(BUILD))/k56enc-dump $(BUILD)/k56dump
# what the caller of a recorded call sent, field by field (tools/v90m_fields.c;
# not installed): the answering modem as v90m-replay runs it, without the
# trace hooks, and the library's receivers on the caller's signal
$(BUILD)/v90m-fields: tools/v90m_fields.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS_ALL) $(INC) -o $@ tools/v90m_fields.c $(LIB_SRC) $(LDLIBS)

# ---- V.90 on the target: record a simulated call on the host, replay it
# (rig/bench_v90.c): CPU cost per phase and a bit-exactness check; the same
# recording through the public API (rig/bench_api.c), static and shared ----
V90_REC := $(BUILD)/v90_call.rec
bench-v90: $(BUILD)/rec_v90 $(BUILD)/bench_v90 $(BUILD)/bench_v90.arm $(BUILD)/bench_api $(BUILD)/bench_api.so $(BUILD)/bench_api.arm
	$(abspath $(BUILD))/rec_v90 -55 0.2 40 0 $(V90_REC) | tail -2
	$(abspath $(BUILD))/bench_v90 $(V90_REC)
	$(QEMU) $(abspath $(BUILD))/bench_v90.arm $(V90_REC) | head -1
	$(abspath $(BUILD))/bench_api $(V90_REC)
	LD_LIBRARY_PATH=$(BUILD) $(abspath $(BUILD))/bench_api.so $(V90_REC)
	$(QEMU) $(abspath $(BUILD))/bench_api.arm $(V90_REC)
$(BUILD)/rec_v90: tools/rec_v90.c $(SIM_SRC) $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ tools/rec_v90.c $(SIM_SRC) $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_v90: rig/bench_v90.c $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ rig/bench_v90.c $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_v90.arm: rig/bench_v90.c $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(ARM_CC) $(T_CFLAGS) -static -o $@ rig/bench_v90.c $(MODEM_SRC) $(LDLIBS)
	$(dir $(ARM_CC))arm-linux-strip $@
$(BUILD)/bench_api: rig/bench_api.c $(STATIC)
	$(CC) $(CFLAGS) -Iinclude -o $@ $< $(STATIC) $(LDLIBS)
$(BUILD)/bench_api.so: rig/bench_api.c $(SHARED)
	$(CC) $(CFLAGS) -Iinclude -o $@ $< -L$(BUILD) -lv90modem $(LDLIBS)
$(BUILD)/bench_api.arm: rig/bench_api.c include/v90modem.h
	$(MAKE) -s BUILD=$(BUILD)/arm CC=$(ARM_CC) $(BUILD)/arm/$(NAME).a
	$(ARM_CC) $(CFLAGS) -Iinclude -static -o $@ $< $(BUILD)/arm/$(NAME).a $(LDLIBS)

# ---- x2 answering: the same with a call of the simulated x2 client
# (tools/rec_x2, replayed by bench_v90 -x); cost-x2 counts ARM instructions
# per second of data mode for x2 and for V.90, as cost-v34 does ----
X2_REC := $(BUILD)/x2_call.rec
bench-x2: $(BUILD)/rec_x2 $(BUILD)/bench_v90 $(BUILD)/bench_v90.arm
	$(abspath $(BUILD))/rec_x2 40 0 $(X2_REC) | tail -2
	$(abspath $(BUILD))/bench_v90 -x $(X2_REC)
	$(QEMU) $(abspath $(BUILD))/bench_v90.arm -x $(X2_REC)
$(BUILD)/rec_x2: tools/rec_x2.c $(SIM_SRC) $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ tools/rec_x2.c $(SIM_SRC) $(MODEM_SRC) $(LDLIBS)
.PHONY: bench-x2 cost-x2
cost-x2: bench-x2 bench-v90
	@head -c $$((8000 * 15)) $(X2_REC) > $(BUILD)/x2_a.rec; head -c $$((8000 * 16)) $(X2_REC) > $(BUILD)/x2_b.rec; \
	 head -c $$((8000 * 15)) $(V90_REC) > $(BUILD)/v90_c.rec; head -c $$((8000 * 16)) $(V90_REC) > $(BUILD)/v90_d.rec; \
	 for r in x2_a x2_b v90_c v90_d; do \
	   case $$r in x2*) x=-x;; *) x=;; esac; \
	   ( $(CNT) $(abspath $(BUILD))/bench_v90.arm $$x $(BUILD)/$$r.rec 2>&1 | grep -c '^Trace' > $(BUILD)/$$r.cnt ) & \
	 done; wait; \
	 a=$$(cat $(BUILD)/x2_a.cnt); b=$$(cat $(BUILD)/x2_b.cnt); c=$$(cat $(BUILD)/v90_c.cnt); d=$$(cat $(BUILD)/v90_d.cnt); \
	 echo "x2 data mode: $$((b - a)) ARM instructions per second of line (second 15 to 16 of the call)"; \
	 echo "V.90 data mode: $$((d - c)) ARM instructions per second of line (second 15 to 16 of the call)"; \
	 echo "ratio x2 / V.90: $$(( (b - a) * 100 / (d - c) )) %"

# ---- V.34 answering: record a simulated call, replay it on host and ARM ----
V34_REC ?= $(BUILD)/v34_call.rec
# cost-v34 counts ARM instructions per second of data mode for V.34 and, the
# same way, for V.90 (whose data mode was timed on the real box), so the two
# can be compared. Slow (minutes): qemu logs every instruction.
CNT = $(QEMU) -one-insn-per-tb -d exec,nochain
.PHONY: cost-v34
cost-v34: bench-v34 bench-v90
	@head -c $$((2 * 8000 * 10)) $(V34_REC) > $(BUILD)/v34_a.rec; head -c $$((2 * 8000 * 11)) $(V34_REC) > $(BUILD)/v34_b.rec; \
	 head -c $$((8000 * 10)) $(V90_REC) > $(BUILD)/v90_a.rec; head -c $$((8000 * 11)) $(V90_REC) > $(BUILD)/v90_b.rec; \
	 a=$$($(CNT) $(abspath $(BUILD))/bench_v34.arm $(BUILD)/v34_a.rec 2>&1 | grep -c '^Trace'); \
	 b=$$($(CNT) $(abspath $(BUILD))/bench_v34.arm $(BUILD)/v34_b.rec 2>&1 | grep -c '^Trace'); \
	 c=$$($(CNT) $(abspath $(BUILD))/bench_v90.arm $(BUILD)/v90_a.rec 2>&1 | grep -c '^Trace'); \
	 d=$$($(CNT) $(abspath $(BUILD))/bench_v90.arm $(BUILD)/v90_b.rec 2>&1 | grep -c '^Trace'); \
	 echo "V.34 data mode: $$((b - a)) ARM instructions per second of line (second 10 to 11 of the call)"; \
	 echo "V.90 data mode: $$((d - c)) ARM instructions per second of line (second 10 to 11 of the call)"; \
	 echo "ratio V.34 / V.90: $$(( (b - a) * 100 / (d - c) )) %"
bench-v34: $(BUILD)/rec_v34 $(BUILD)/bench_v34 $(BUILD)/bench_v34.arm
	$(abspath $(BUILD))/rec_v34 16 $(V34_REC)
	$(abspath $(BUILD))/bench_v34 $(V34_REC)
	$(QEMU) $(abspath $(BUILD))/bench_v34.arm $(V34_REC) | head -1
$(BUILD)/rec_v34: tools/rec_v34.c rig/v34glue.h $(SIM_SRC) $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ tools/rec_v34.c $(SIM_SRC) $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_v34: rig/bench_v34.c rig/v34glue.h $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ rig/bench_v34.c $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_v34.arm: rig/bench_v34.c rig/v34glue.h $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(ARM_CC) $(T_CFLAGS) -static -o $@ rig/bench_v34.c $(MODEM_SRC) $(LDLIBS)

# ---- V.90 analogue modem, the downstream receiver (modem/v90arx): record a
# simulated downstream on the host (tools/rec_v90a: 2 km, +100 ppm, echo of
# our V.34 upstream 8 dB over the downstream, 48000 bit/s), replay it on the
# host and under qemu; the decision hash must agree ----
V90A_REC := $(BUILD)/v90a.rec
bench-v90a: $(BUILD)/rec_v90a $(BUILD)/bench_v90a $(BUILD)/bench_v90a.arm
	$(abspath $(BUILD))/rec_v90a 100 48000 $(V90A_REC)
	$(abspath $(BUILD))/bench_v90a $(V90A_REC)
	$(QEMU) $(abspath $(BUILD))/bench_v90a.arm $(V90A_REC)
# cost-v90a counts ARM instructions as cost-v34 does: per second of line in
# data mode (seconds 3 to 4; data from 1.2 s), and over Sd and TRN1d
# (0.55 to 1.25 s: the clock measured, the estimate, the first computation
# of the equaliser), at 0 ppm (the equaliser computed again every 2048
# symbols) and 100 ppm (every 512). Slow: qemu logs every instruction.
.PHONY: cost-v90a
cost-v90a: $(BUILD)/rec_v90a $(BUILD)/bench_v90a.arm
	@for p in 0 100; do \
	   $(abspath $(BUILD))/rec_v90a $$p 48000 $(BUILD)/v90a_$$p.rec > /dev/null; \
	   for s in 4400 10000 24000 32000; do head -c $$((36 + 4 * s)) $(BUILD)/v90a_$$p.rec > $(BUILD)/v90a_$$p.$$s; done; \
	   a=$$($(CNT) $(abspath $(BUILD))/bench_v90a.arm $(BUILD)/v90a_$$p.4400 2>&1 | grep -c '^Trace'); \
	   b=$$($(CNT) $(abspath $(BUILD))/bench_v90a.arm $(BUILD)/v90a_$$p.10000 2>&1 | grep -c '^Trace'); \
	   c=$$($(CNT) $(abspath $(BUILD))/bench_v90a.arm $(BUILD)/v90a_$$p.24000 2>&1 | grep -c '^Trace'); \
	   d=$$($(CNT) $(abspath $(BUILD))/bench_v90a.arm $(BUILD)/v90a_$$p.32000 2>&1 | grep -c '^Trace'); \
	   echo "V.90 downstream receiver, $$p ppm: data mode $$((d - c)) ARM instructions per second of line (second 3 to 4);" \
	        "Sd and TRN1d (0.7 s) $$((b - a))"; \
	 done
$(BUILD)/rec_v90a: tools/rec_v90a.c $(SIM_SRC) $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ tools/rec_v90a.c $(SIM_SRC) $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_v90a: rig/bench_v90a.c $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ rig/bench_v90a.c $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_v90a.arm: rig/bench_v90a.c $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(ARM_CC) $(T_CFLAGS) -static -o $@ rig/bench_v90a.c $(MODEM_SRC) $(LDLIBS)

# ---- x2 against an emulated U.S. Robotics Courier (tools/x2-peer/README.md):
# courier-emu and the ROM live outside the repository; not part of check
COURIER_EMU ?=
PAD  ?= 0
MODS ?=
STRICT_RENEG ?= 0
$(BUILD)/x2-bridge: tools/x2-peer/bridge.c $(STATIC) include/v90modem.h
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -Iinclude -o $@ $< $(STATIC) $(LDLIBS)
test-x2-courier:
	@if [ -z "$(COURIER_EMU)" ]; then echo "COURIER_EMU not set"; exit 0; fi; \
	$(MAKE) -s $(BUILD)/x2-bridge && \
	COURIER_EMU="$(COURIER_EMU)" PAD="$(PAD)" MODS="$(MODS)" STRICT_RENEG="$(STRICT_RENEG)" sh tools/x2-peer/run.sh $(abspath $(BUILD))/x2-bridge $(abspath $(BUILD))/x2-peer

# ---- K56flex against Motorola's SM56 (tools/k56-peer/README.md): D-Modem's
# half_sm56 and sm56.ko live outside the repository; not part of check
DMODEM ?=
RBS  ?= 0
DOWN ?= 0
$(BUILD)/k56-bridge: tools/k56-peer/bridge.c $(STATIC) include/v90modem.h
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -Iinclude -o $@ $< $(STATIC) $(LDLIBS)
test-k56-sm56:
	@if [ -z "$(DMODEM)" ]; then echo "DMODEM not set"; exit 0; fi; \
	if ! printf 'int main(void) { return 0; }\n' | $(CC) -m32 -x c - -o /dev/null 2>/dev/null; then \
		echo "no 32-bit toolchain (glibc-devel.i686)"; exit 0; fi; \
	$(MAKE) -s $(BUILD)/k56-bridge && \
	DMODEM="$(DMODEM)" MODS="$(MODS)" RBS="$(RBS)" DOWN="$(DOWN)" STRICT_RENEG="$(STRICT_RENEG)" sh tools/k56-peer/run.sh $(abspath $(BUILD))/k56-bridge $(abspath $(BUILD))/k56-peer

clean:
	rm -rf $(BUILD)
	@for d in $(SUBS); do $(MAKE) -s -C $$d BUILD=build clean; done

# ---- V.90 analogue modem, the whole caller (modem/orig, modem/v90a): record
# what its A/D receives in a simulated call against the library's answerer
# (tools/rec_v90c: 2 km, +100 ppm, until 5 s after data mode), replay it on
# the host and under qemu; the transmit hash must agree ----
V90C_REC := $(BUILD)/v90c.rec
bench-v90c: $(BUILD)/rec_v90c $(BUILD)/bench_v90c $(BUILD)/bench_v90c.arm
	$(abspath $(BUILD))/rec_v90c 100 5 $(V90C_REC)
	$(abspath $(BUILD))/bench_v90c $(V90C_REC)
	$(QEMU) $(abspath $(BUILD))/bench_v90c.arm $(V90C_REC)
# cost-v90c counts ARM instructions as cost-v90a does: per second of line in
# data mode (seconds 2 to 3 after data mode began), at 0 and 100 ppm; then
# each of the 4 ticks that took longest on the host (100 ppm), counted alone
# (the recording cut just after it, less the recording cut just before it).
# Slow: qemu logs every instruction.
.PHONY: cost-v90c
cost-v90c: $(BUILD)/rec_v90c $(BUILD)/bench_v90c $(BUILD)/bench_v90c.arm
	@for p in 0 100; do \
	   $(abspath $(BUILD))/rec_v90c $$p 4 $(BUILD)/v90c_$$p.rec > /dev/null; \
	   d=$$(od -An -tu4 -j8 -N4 $(BUILD)/v90c_$$p.rec | tr -d ' '); \
	   head -c $$((12 + 2 * (d + 16000))) $(BUILD)/v90c_$$p.rec > $(BUILD)/v90c_$$p.a; \
	   head -c $$((12 + 2 * (d + 24000))) $(BUILD)/v90c_$$p.rec > $(BUILD)/v90c_$$p.b; \
	   a=$$($(CNT) $(abspath $(BUILD))/bench_v90c.arm $(BUILD)/v90c_$$p.a 2>&1 | grep -c '^Trace'); \
	   b=$$($(CNT) $(abspath $(BUILD))/bench_v90c.arm $(BUILD)/v90c_$$p.b 2>&1 | grep -c '^Trace'); \
	   echo "V.90 caller, $$p ppm: data mode $$((b - a)) ARM instructions per second of line (seconds 2 to 3 of data mode)"; \
	 done; \
	 for t in $$($(abspath $(BUILD))/bench_v90c $(BUILD)/v90c_100.rec -w 4 | sed -n 's/^slow tick \([0-9]*\) .*/\1/p'); do \
	   head -c $$((12 + 2 * t)) $(BUILD)/v90c_100.rec > $(BUILD)/v90c_t.a; \
	   head -c $$((12 + 2 * (t + 1))) $(BUILD)/v90c_100.rec > $(BUILD)/v90c_t.b; \
	   a=$$($(CNT) $(abspath $(BUILD))/bench_v90c.arm $(BUILD)/v90c_t.a 2>&1 | grep -c '^Trace'); \
	   b=$$($(CNT) $(abspath $(BUILD))/bench_v90c.arm $(BUILD)/v90c_t.b 2>&1 | grep -c '^Trace'); \
	   echo "V.90 caller, 100 ppm: tick $$t ($$((t / 8)) ms): $$((b - a)) ARM instructions"; \
	 done
# ---- the same for the caller in V.92 (PCM upstream): rec_v90c ... v92 records
# a V.92 call (the upstream through the answerer's own V.92 line), and
# bench_v90c replays it with V.92 on ----
V92C_REC := $(BUILD)/v92c.rec
bench-v92c: $(BUILD)/rec_v90c $(BUILD)/bench_v90c $(BUILD)/bench_v90c.arm
	$(abspath $(BUILD))/rec_v90c 100 5 $(V92C_REC) v92
	$(abspath $(BUILD))/bench_v90c $(V92C_REC)
	$(QEMU) $(abspath $(BUILD))/bench_v90c.arm $(V92C_REC)
.PHONY: cost-v92c
cost-v92c: $(BUILD)/rec_v90c $(BUILD)/bench_v90c $(BUILD)/bench_v90c.arm
	@for p in 0 100; do \
	   $(abspath $(BUILD))/rec_v90c $$p 4 $(BUILD)/v92c_$$p.rec v92 > /dev/null; \
	   d=$$(od -An -tu4 -j8 -N4 $(BUILD)/v92c_$$p.rec | tr -d ' '); \
	   head -c $$((12 + 2 * (d + 16000))) $(BUILD)/v92c_$$p.rec > $(BUILD)/v92c_$$p.a; \
	   head -c $$((12 + 2 * (d + 24000))) $(BUILD)/v92c_$$p.rec > $(BUILD)/v92c_$$p.b; \
	   a=$$($(CNT) $(abspath $(BUILD))/bench_v90c.arm $(BUILD)/v92c_$$p.a 2>&1 | grep -c '^Trace'); \
	   b=$$($(CNT) $(abspath $(BUILD))/bench_v90c.arm $(BUILD)/v92c_$$p.b 2>&1 | grep -c '^Trace'); \
	   echo "V.92 caller, $$p ppm: data mode $$((b - a)) ARM instructions per second of line (seconds 2 to 3 of data mode)"; \
	 done; \
	 for t in $$($(abspath $(BUILD))/bench_v90c $(BUILD)/v92c_100.rec -w 6 | sed -n 's/^slow tick \([0-9]*\) .*/\1/p'); do \
	   head -c $$((12 + 2 * t)) $(BUILD)/v92c_100.rec > $(BUILD)/v92c_t.a; \
	   head -c $$((12 + 2 * (t + 1))) $(BUILD)/v92c_100.rec > $(BUILD)/v92c_t.b; \
	   a=$$($(CNT) $(abspath $(BUILD))/bench_v90c.arm $(BUILD)/v92c_t.a 2>&1 | grep -c '^Trace'); \
	   b=$$($(CNT) $(abspath $(BUILD))/bench_v90c.arm $(BUILD)/v92c_t.b 2>&1 | grep -c '^Trace'); \
	   echo "V.92 caller, 100 ppm: tick $$t ($$((t / 8)) ms): $$((b - a)) ARM instructions"; \
	 done
# prof-v90c / prof-v92c: the cost of every tick and the profile, from one qemu
# run each (much faster than cost-*: no -one-insn-per-tb). rec_v90c records a
# call at PPM (default 100) with 4 s of data mode; bench_v90c -t replays it
# up to 3 s into data mode, calling a mark before every tick; tools/qprof
# turns qemu's log (blocks as translated, each block as run) into the
# instructions of each tick (the heaviest listed) and per function over
# seconds 2 to 3 of data mode. QPROF_TB=N lists the N heaviest blocks too.
PPM ?= 100
.PHONY: prof-v90c prof-v92c
prof-v90c prof-v92c: $(BUILD)/rec_v90c $(BUILD)/bench_v90c.arm $(BUILD)/qprof
	@r=$(BUILD)/$@_$(PPM).rec; \
	 $(abspath $(BUILD))/rec_v90c $(PPM) 4 $$r $(if $(filter prof-v92c,$@),v92) > /dev/null; \
	 d=$$(od -An -tu4 -j8 -N4 $$r | tr -d ' '); \
	 pc=$$(nm $(BUILD)/bench_v90c.arm | sed -n 's/^\([0-9a-f]*\) T bench_v90c_mark$$/\1/p'); \
	 $(QEMU) -d in_asm,exec,nochain -D /dev/stdout $(abspath $(BUILD))/bench_v90c.arm $$r -t -n $$((d + 24000)) 2>/dev/null \
	 | $(abspath $(BUILD))/qprof $$pc $$((d + 16000)) $$((d + 24000)) -top 12 $(if $(QPROF_TB),-tb $(QPROF_TB))
$(BUILD)/qprof: tools/qprof.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<
# ---- the caller in Bell 103 and Bell 212A: rec_bell records a call against
# the library's answerer, bench_bell replays it into the caller alone (hash on
# the host and under qemu); cost-bell counts the ARM instructions of the
# first second of data mode, one qemu run per call (split where the trace
# enters bench_bell_mark) ----
.PHONY: bench-bell cost-bell
bench-bell: $(BUILD)/rec_bell $(BUILD)/bench_bell $(BUILD)/bench_bell.arm
	@for m in 103 212; do \
	   $(abspath $(BUILD))/rec_bell $$m 3 $(BUILD)/bell_$$m.rec; \
	   $(abspath $(BUILD))/bench_bell $(BUILD)/bell_$$m.rec; \
	   $(QEMU) $(abspath $(BUILD))/bench_bell.arm $(BUILD)/bell_$$m.rec; \
	 done
cost-bell: $(BUILD)/rec_bell $(BUILD)/bench_bell.arm
	@for m in 103 212; do \
	   $(abspath $(BUILD))/rec_bell $$m 2 $(BUILD)/bell_$$m.rec > /dev/null; \
	   pc=$$(nm $(BUILD)/bench_bell.arm | sed -n 's/^\([0-9a-f]*\) T bench_bell_mark$$/\1/p'); \
	   $(CNT) $(abspath $(BUILD))/bench_bell.arm $(BUILD)/bell_$$m.rec 2>&1 >/dev/null \
	   | awk -v m=$$m -v pc="/00000000$$pc/" 'BEGIN { k = 0 } /^Trace/ { if (index($$0, pc)) k++; n[k]++ } END { \
	       printf "Bell %s caller: %d ARM instructions to data mode, %d in its first second, %d after\n", m == 212 ? "212A" : "103", n[0], n[1], n[2] }'; \
	 done
$(BUILD)/rec_bell: tools/rec_bell.c $(SIM_SRC) $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ tools/rec_bell.c $(SIM_SRC) $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_bell: rig/bench_bell.c $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ rig/bench_bell.c $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_bell.arm: rig/bench_bell.c $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(ARM_CC) $(T_CFLAGS) -static -o $@ rig/bench_bell.c $(MODEM_SRC) $(LDLIBS)
$(BUILD)/rec_v90c: tools/rec_v90c.c test/v90link.h $(SIM_SRC) $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -Itest -o $@ tools/rec_v90c.c $(SIM_SRC) $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_v90c: rig/bench_v90c.c $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -o $@ rig/bench_v90c.c $(MODEM_SRC) $(LDLIBS)
$(BUILD)/bench_v90c.arm: rig/bench_v90c.c $(MODEM_SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(ARM_CC) $(T_CFLAGS) -static -o $@ rig/bench_v90c.c $(MODEM_SRC) $(LDLIBS)
# a simulated call of the library's caller, recorded at the answerer as v90d
# records real ones (tools/rec_orig.c; host only)
$(BUILD)/rec_orig: tools/rec_orig.c $(SIM_SRC) $(MODEM_SRC) api/v90modem.c $(HDRS) test/v90link.h test/line34.h
	@mkdir -p $(BUILD)
	$(CC) $(T_CFLAGS) -Iinclude -Iapi -o $@ tools/rec_orig.c api/v90modem.c $(SIM_SRC) $(MODEM_SRC) $(addprefix v42/,v42_hdlc.c v42_detect.c v42_comp.c v44_comp.c v42_lapm.c v42.c) $(LDLIBS)
