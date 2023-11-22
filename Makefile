.PHONY: all clean test_scripter

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

O ?=
ifneq ($(O),)
o_opt := -O$(O)
else
o_opt :=
endif

CFLAGS += -Wall $(o_opt)
CPPFLAGS += -Isrc
LDFLAGS += -Wall
CC := gcc
LD := gcc
DEPDIR := .deps
OBJDIR := .objs
SRCDIR := src
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

quiet_cmd_cc = CC $@
      cmd_cc = $(CC) $(DEPFLAGS) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
quiet_cmd_ld = LD $@
      cmd_ld = $(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

prog := ./egq

srcs := $(wildcard $(SRCDIR)/builtin/*.c) $(wildcard $(SRCDIR)/*.c)
objs := $(patsubst $(SRCDIR)/%,$(OBJDIR)/%,$(patsubst %.c,%.o,$(srcs)))

# Inspired from Linux's scripts/Kbuild.include file
#
# $(call cmd,cmd-name) -- from normal rules
basic_echo = echo '    $(1)'
cmd = @$(if $($(quiet)cmd_$(1)), \
            $(call basic_echo,$($(quiet)cmd_$(1))) ;) $(cmd_$(1))

all: $(prog)

dir_targets := $(DEPDIR) $(OBJDIR) $(DEPDIR)/builtin $(OBJDIR)/builtin
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

cleanfiles := $(OBJDIR) $(prog) $(DEPDIR)
clean:
	$(if $(wildcard $(cleanfiles)),$(RM) -rf $(wildcard $(cleanfiles)),\
	  @echo "Nothing to clean")

ifneq ($(MAKECMDGOALS),clean)
include $(wildcard $(DEPFILES))
endif

