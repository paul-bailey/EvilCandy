.PHONY: all clean test_scripter release

# command-line options:
#         V=1 : verbose
#         V=0 : pretty (default)
#         -s  : stderr-only
#
# For this to work, use $(call cmd,...), which could be used like so:
#
# "
#          quiet_cmd_cc = CC $@
#                cmd_cc = $(CC) -c $(CFLAGS) -o $@ $^
#
#          %.o: %.c
#                  $(call cmd,cc)
# "
ifeq ("$(origin V)", "command line")
 VERBOSE = $(V)
endif
ifndef VERBOSE
 VERBOSE = 0
endif
ifeq ($(VERBOSE),0)
 Q     = @
 quiet = quiet_
else
 Q =
 quiet =
endif

ifneq ($(filter s% -s%,$(MAKEFLAGS)),)
  quiet=silent_
endif

pwd := $(shell pwd)

CFLAGS += -Wall
CPPFLAGS += -Iinc
ifeq ($(MAKECMDGOALS),release)
CFLAGS += -O3
CPPFLAGS += -DNDEBUG
endif
LDFLAGS += -Wall
CC := gcc
LD := gcc
DEPDIR := .deps
OBJDIR := .objs
SRCDIR := src
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
cleanfiles :=

quiet_cmd_cc = CC $@
      cmd_cc = $(CC) $(DEPFLAGS) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
quiet_cmd_ld = LD $@
      cmd_ld = $(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

prog := ./egq

srcs := \
 $(wildcard $(SRCDIR)/types/*.c) \
 $(wildcard $(SRCDIR)/builtin/*.c) \
 $(wildcard $(SRCDIR)/*.c)

objs := $(patsubst $(SRCDIR)/%,$(OBJDIR)/%,$(patsubst %.c,%.o,$(srcs)))

# Inspired from Linux's scripts/Kbuild.include file
#
# $(call cmd,cmd-name) -- from normal rules
basic_echo = echo '    $(1)'
cmd = @$(if $($(quiet)cmd_$(1)), \
            $(call basic_echo,$($(quiet)cmd_$(1))) ;) $(cmd_$(1))

all: $(prog)
release: all

dir_targets := \
 $(DEPDIR) $(OBJDIR) \
 $(DEPDIR)/builtin $(OBJDIR)/builtin \
 $(DEPDIR)/types $(OBJDIR)/types

$(OBJDIR)/%.o: $(SRCDIR)/%.c
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(DEPDIR)/%.d | $(dir_targets)
	$(call cmd,cc)

$(dir_targets): ; @mkdir -p $@

DEPFILES := $(objs:$(OBJDIR)/%.o=$(DEPDIR)/%.d)
$(DEPFILES):

$(prog): $(objs)
	$(call cmd,ld)

test_scripter: $(prog) demo.tpl
	$(prog) demo.tpl

##
# generated files

# FIXME: This is a brute-force way to force regeneration of
# inc/instruction_defs.h.  Any changes to tools/instructions will force
# recompilation of all objects, even though only a few of them require
# inc/instruction_defs.h.  But it's the only way I know how to force
# genaration of the header at all.
$(objs): inc/instruction_defs.h

$(OBJDIR)/disassemble.o: $(SRCDIR)/disassemble_gen.c.h
$(OBJDIR)/vm.o: $(SRCDIR)/vm_gen.c.h

gen := tools/gen
list := tools/instructions
gentool = cat $(list) | $(gen) $1 > $@

quiet_cmd_gentool = GENTOOL $2 $@
      cmd_gentool = $(call gentool,$2)

inc/instruction_defs.h: $(list) $(gen)
	$(call cmd,gentool,def)

$(SRCDIR)/vm_gen.c.h: $(list) $(gen)
	$(call cmd,gentool,jump)

$(SRCDIR)/disassemble_gen.c.h: $(list) $(gen)
	$(call cmd,gentool,dis)

quiet_cmd_gen_gen = CC $@
      cmd_gen_gen = cc -Wall $< -o $@
$(gen): $(gen).c
	$(call cmd,gen_gen)

cleanfiles += $(wildcard $(SRCDIR)/*_gen.c.h)
cleanfiles += inc/instruction_defs.h
cleanfiles += $(gen)

##
# cleanup

cleanfiles += $(OBJDIR) $(prog) $(DEPDIR)
clean:
	$(if $(wildcard $(cleanfiles)),$(RM) -rf $(wildcard $(cleanfiles)),\
	  @echo "Nothing to clean")

ifneq ($(MAKECMDGOALS),clean)
include $(wildcard $(DEPFILES))
endif

