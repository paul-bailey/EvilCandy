ACLOCAL_AMFLAGS = -I build/m4
AM_CFLAGS = \
 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -pedantic
AM_CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"

bin_PROGRAMS = evilcandy tests/c/test_path
check_PROGRAMS = tools/tokgen tools/gen

tools_tokgen_SOURCES = tools/tokgen.c tools/genutil.c
tools_gen_SOURCES = tools/gen.c
tools_gen_CPPFLAGS = -I$(top_srcdir)/inc

COMMON_SOURCES = \
        src/assembler.c \
        src/assemble_post.c \
        src/buffer.c \
        src/ctype.c \
        src/cwd.c \
        src/debug.c \
        src/disassemble.c \
        src/err.c \
        src/errmsg.c \
        src/ewrappers.c \
        src/global.c \
        src/hash.c \
        src/helpers.c \
        src/instruction_name.c \
        src/init.c \
        src/iterator.c \
        src/json.c \
        src/list.c \
        src/locations.c \
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
        src/vm_getargs.c \
        src/builtin/builtin.c \
        src/builtin/io.c \
        src/builtin/math.c \
        src/builtin/socket.c \
        src/builtin/sys.c \
        src/types/array.c \
        src/types/bytes.c \
        src/types/cell.c \
        src/types/class.c \
        src/types/complex.c \
        src/types/dict.c \
        src/types/empty.c \
        src/types/generator.c \
        src/types/float.c \
        src/types/function.c \
        src/types/integer.c \
        src/types/intl.c \
        src/types/method.c \
        src/types/property.c \
        src/types/range.c \
        src/types/set.c \
        src/types/string.c \
        src/types/tuple.c \
        src/types/xptr.c

tests_c_test_path_SOURCES = \
        $(COMMON_SOURCES) \
        tests/c/test_path.c

evilcandy_SOURCES = \
        $(COMMON_SOURCES) \
        src/main.c

noinst_HEADERS = \
        inc/debug.h \
        inc/evcenums.h \
        inc/evilcandy.h \
        inc/global.h \
        inc/hash.h \
        inc/instructions.h \
        inc/iterator.h \
        inc/objtypes.h \
        inc/recursion.h \
        inc/string_reader.h \
        inc/string_writer.h \
        inc/typedefs.h \
        inc/uarg.h \
        inc/var.h \
        inc/vm.h \
        inc/evilcandy/init.h \
        inc/evilcandy/token.h \
        inc/evilcandy/assemble.h \
        inc/evilcandy/locations.h \
        inc/types/xptr.h \
        inc/lib/buffer.h \
        inc/lib/helpers.h \
        inc/lib/list.h \
        inc/lib/utf8.h \
        tools/gen.h

COMMON_LDADD=-lm
if WANT_READLINE
COMMON_LDADD+=-lreadline
endif
evilcandy_LDADD=$(COMMON_LDADD)
tests_c_test_path_LDADD=$(COMMON_LDADD)

evilcandydir=${datadir}/evilcandy
# well, common except to source code generators
COMMON_CPPFLAGS=\
 -I$(top_srcdir)/inc \
 -DRCDATADIR=\"$(evilcandydir)\" \
 -Wall

evilcandy_CPPFLAGS=$(COMMON_CPPFLAGS)
tests_c_test_path_CPPFLAGS=$(COMMON_CPPFLAGS)

dist_evilcandy_DATA= \
        lib/enum.evc \
        lib/io.evc \
        lib/math.evc \
        lib/socket.evc \
        lib/test.evc

nodist_COMMON_SOURCES = \
        inc/instruction_defs.h \
        inc/token_gen.h \
        src/disassemble_gen.c.h \
        src/tokutils_gen.c \
        src/vm_gen.c.h
nodist_evilcandy_SOURCES = $(nodist_COMMON_SOURCES)
nodist_tests_c_test_path_SOURCES = $(nodist_COMMON_SOURCES)

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
        ROADMAP.md \
        AUTHORS \
        COPYING \
        README \
        etc/thought_bucket.txt


