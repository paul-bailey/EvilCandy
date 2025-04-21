bin_PROGRAMS = evilcandy
evilcandy_SOURCES = \
assembler.c \
buffer.c \
disassemble.c \
err.c \
ewrappers.c \
find_import.c \
hash.c \
helpers.c \
json.c \
list.c \
main.c \
op.c \
path.c \
serializer.c \
token.c \
uuid.c \
var.c \
vm.c \
builtin/builtin.c \
builtin/io.c \
builtin/math.c \
types/array.c \
types/bytes.c \
types/dict.c \
types/empty.c \
types/file.c \
types/float.c \
types/function.c \
types/integer.c \
types/intl.c \
types/range.c \
types/string.c \
types/xptr.c

evilcandy_CPPFLAGS=-I$(top_srcdir)/inc -DRCDATADIR=\"$(datadir)\"

nodist_evilcandy_SOURCES = \
 instruction_defs.h \
 disassemble_gen.c.h \
 vm_gen.c.h

BUILT_SOURCES= \
 instruction_defs.h \
 disassemble_gen.c.h \
 vm_gen.c.h

CLEANFILES = \
 instruction_defs.h \
 disassemble_gen.c.h \
 vm_gen.c.h

disassemble_gen.c.h: $(top_srcdir)/tools/instructions $(top_srcdir)/tools/gen
	cat $(top_srcdir)/tools/instructions | $(top_srcdir)/tools/gen dis > $@

instruction_defs.h: $(top_srcdir)/tools/instructions $(top_srcdir)/tools/gen
	cat $(top_srcdir)/tools/instructions | $(top_srcdir)/tools/gen def > $@

vm_gen.c.h: $(top_srcdir)/tools/instructions $(top_srcdir)/tools/gen
	cat $(top_srcdir)/tools/instructions | $(top_srcdir)/tools/gen jump > $@

