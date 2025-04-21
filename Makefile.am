ACLOCAL_AMFLAGS = -I build/m4

bin_PROGRAMS = tools/gen evilcandy

tools_gen_SOURCES = tools/gen.c
tools_gen_CPPFLAGS = -I$(top_srcdir)/inc

evilcandy_SOURCES = \
        src/assembler.c \
        src/buffer.c \
        src/disassemble.c \
        src/err.c \
        src/ewrappers.c \
        src/find_import.c \
        src/hash.c \
        src/helpers.c \
        src/json.c \
        src/list.c \
        src/main.c \
        src/op.c \
        src/path.c \
        src/serializer.c \
        src/token.c \
        src/uuid.c \
        src/var.c \
        src/vm.c \
        src/builtin/builtin.c \
        src/builtin/io.c \
        src/builtin/math.c \
        src/types/array.c \
        src/types/bytes.c \
        src/types/dict.c \
        src/types/empty.c \
        src/types/file.c \
        src/types/float.c \
        src/types/function.c \
        src/types/integer.c \
        src/types/intl.c \
        src/types/range.c \
        src/types/string.c \
        src/types/xptr.c

evilcandydir=${datadir}/evilcandy
evilcandy_CPPFLAGS=-I$(top_srcdir)/inc -DRCDATADIR=\"$(evilcandydir)\"

evilcandy_DATA= \
        lib/io.evc \
        lib/math.evc

nodist_evilcandy_SOURCES = \
        inc/instruction_defs.h \
        src/disassemble_gen.c.h \
        src/vm_gen.c.h

BUILT_SOURCES= \
        inc/instruction_defs.h \
        src/disassemble_gen.c.h \
        src/vm_gen.c.h

CLEANFILES = \
        inc/instruction_defs.h \
        src/disassemble_gen.c.h \
        src/vm_gen.c.h

src/disassemble_gen.c.h: tools/instructions tools/gen
	cat tools/instructions | tools/gen dis > $@

inc/instruction_defs.h: tools/instructions tools/gen
	cat tools/instructions | tools/gen def > $@

src/vm_gen.c.h: tools/instructions tools/gen
	cat tools/instructions | tools/gen jump > $@

dist_doc_DATA = README Tutorial.rst

