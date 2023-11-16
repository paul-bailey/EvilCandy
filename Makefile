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

CFLAGS += -Wall
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

objs := $(patsubst %.c,%.o,$(wildcard $(SRCDIR)/*.c))
objs := $(addprefix $(OBJDIR)/,$(notdir $(objs)))

# Inspired from Linux's scripts/Kbuild.include file
#
# $(call cmd,cmd-name) -- from normal rules
basic_echo = echo '    $(1)'
cmd = @$(if $($(quiet)cmd_$(1)), \
            $(call basic_echo,$($(quiet)cmd_$(1))) ;) $(cmd_$(1))


all: $(prog)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(DEPDIR)/%.d | $(DEPDIR) $(OBJDIR)
	$(call cmd,cc)

$(DEPDIR) $(OBJDIR): ; @mkdir -p $@

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

