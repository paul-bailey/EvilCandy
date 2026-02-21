ACLOCAL_AMFLAGS = -I build/m4

bin_PROGRAMS = evilcandy
check_PROGRAMS = tools/tokgen tools/gen

tools_tokgen_SOURCES = tools/tokgen.c tools/genutil.c
tools_gen_SOURCES = tools/gen.c
tools_gen_CPPFLAGS = -I$(top_srcdir)/inc

evilcandy_SOURCES = \
        src/assembler.c \
        src/assemble_post.c \
        src/buffer.c \
        src/ctype.c \
        src/disassemble.c \
        src/err.c \
        src/errmsg.c \
        src/ewrappers.c \
        src/global.c \
        src/hash.c \
        src/helpers.c \
        src/instruction_name.c \
        src/json.c \
        src/list.c \
        src/main.c \
        src/op.c \
        src/path.c \
        src/readline.c \
        src/reassemble.c \
        src/string_writer.c \
        src/strto.c \
        src/token.c \
        src/utf8.c \
        src/uuid.c \
        src/var.c \
        src/var_from_format.c \
        src/vm.c \
        src/builtin/builtin.c \
        src/builtin/io.c \
        src/builtin/math.c \
        src/types/array.c \
        src/types/bytes.c \
        src/types/complex.c \
        src/types/dict.c \
        src/types/empty.c \
        src/types/file.c \
        src/types/float.c \
        src/types/floats.c \
        src/types/function.c \
        src/types/integer.c \
        src/types/intl.c \
        src/types/method.c \
        src/types/property.c \
        src/types/range.c \
        src/types/star.c \
        src/types/string.c \
        src/types/tuple.c \
        src/types/xptr.c

noinst_HEADERS = \
        src/assemble_priv.h \
        inc/debug.h \
        inc/evcenums.h \
        inc/evilcandy.h \
        inc/global.h \
        inc/instructions.h \
        inc/objtypes.h \
        inc/recursion.h \
        inc/string_reader.h \
        inc/string_writer.h \
        inc/token.h \
        inc/typedefs.h \
        inc/uarg.h \
        inc/var.h \
        inc/vm.h \
        inc/xptr.h \
        inc/lib/buffer.h \
        inc/lib/helpers.h \
        inc/lib/list.h \
        tools/gen.h

if WANT_READLINE
evilcandy_LDADD=-lreadline
endif

evilcandydir=${datadir}/evilcandy
evilcandy_CPPFLAGS=\
 -I$(top_srcdir)/inc \
 -DRCDATADIR=\"$(evilcandydir)\" \
 -Wall

dist_evilcandy_DATA= \
        lib/io.evc \
        lib/math.evc

nodist_evilcandy_SOURCES = \
        inc/instruction_defs.h \
        inc/token_gen.h \
        src/disassemble_gen.c.h \
        src/tokutils_gen.c \
        src/vm_gen.c.h

BUILT_SOURCES= \
        inc/instruction_defs.h \
        inc/token_gen.h \
        src/disassemble_gen.c.h \
        src/tokutils_gen.c \
        src/vm_gen.c.h

CLEANFILES = \
        inc/instruction_defs.h \
        inc/token_gen.h \
        src/disassemble_gen.c.h \
        src/tokutils_gen.c \
        src/vm_gen.c.h

src/disassemble_gen.c.h: tools/instructions tools/gen
	cat tools/instructions | tools/gen dis > $@

inc/instruction_defs.h: tools/instructions tools/gen
	cat tools/instructions | tools/gen def > $@

inc/token_gen.h: tools/tokens tools/tokgen
	cat tools/tokens | tools/tokgen enum > $@

src/tokutils_gen.c: tools/tokens tools/tokgen
	cat tools/tokens | tools/tokgen util > $@

src/vm_gen.c.h: tools/instructions tools/gen
	cat tools/instructions | tools/gen jump > $@

EXTRA_DIST = \
        tools/instructions \
        tools/tokens \
        source_tree_layout.txt \
        to-do.txt \
        AUTHORS \
        COPYING \
        README \
        etc/thought_bucket.txt


