# Do this instead of check_PROGRAMS, since I do not necessarily
# want to run this during 'make check'
.PHONY: tests
tests: programs/unit_tests programs/fuzzer

ACLOCAL_AMFLAGS = -I build/m4
AM_CFLAGS = \
 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -pedantic \
 -Wmissing-prototypes -Wmissing-declarations \
 -Wredundant-decls -Wstrict-prototypes
AM_CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"

bin_PROGRAMS = evilcandy
EXTRA_PROGRAMS = programs/unit_tests programs/fuzzer
check_PROGRAMS = tools/tokgen tools/gen

noinst_LIBRARIES = src/libevilcandy.a

tools_tokgen_SOURCES = tools/tokgen.c tools/genutil.c
tools_gen_SOURCES = tools/gen.c
tools_gen_CPPFLAGS = -I$(top_srcdir)/inc

src_libevilcandy_a_SOURCES = \
        src/assembler.c \
        src/assemble_post.c \
        src/buffer.c \
        src/ctype.c \
        src/cwd.c \
        src/debug.c \
        src/disassemble.c \
        src/codec.c \
        src/err.c \
        src/errmsg.c \
        src/ewrappers.c \
        src/global.c \
        src/hash.c \
        src/helpers.c \
        src/import.c \
        src/instruction_name.c \
        src/init.c \
        src/iterator.c \
        src/list.c \
        src/locations.c \
        src/namespace.c \
        src/op.c \
        src/path.c \
        src/readline.c \
        src/reassemble.c \
        src/string_writer.c \
        src/strto.c \
        src/token.c \
        src/utf8.c \
        src/var.c \
        src/var_from_format.c \
        src/vm.c \
        src/vm_getargs.c \
        src/builtin/builtin.c \
        src/builtin/io.c \
        src/builtin/json.c \
        src/builtin/math.c \
        src/builtin/socket.c \
        src/builtin/sys.c \
        src/builtin/uuid.c \
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

programs_unit_tests_SOURCES = programs/unit_tests.c
programs_fuzzer_SOURCES = programs/fuzzer.c
evilcandy_SOURCES = programs/evilcandy.c

noinst_HEADERS = \
        inc/compiler.h \
        inc/debug.h \
        inc/evcenums.h \
        inc/evilcandy.h \
        inc/global.h \
        inc/hash.h \
        inc/instructions.h \
        inc/iterator.h \
        inc/recursion.h \
        inc/string_reader.h \
        inc/string_writer.h \
        inc/typedefs.h \
        inc/uarg.h \
        inc/var.h \
        inc/vm.h \
        inc/internal/codec.h \
        inc/internal/cwd.h \
        inc/internal/err.h \
        inc/internal/global.h \
        inc/internal/import.h \
        inc/internal/path.h \
        inc/internal/token.h \
        inc/internal/assemble.h \
        inc/internal/init.h \
        inc/internal/op.h \
        inc/internal/type_protocol.h \
        inc/internal/type_registry.h \
        inc/internal/vm.h \
        inc/internal/locations.h \
        inc/internal/builtin/io.h \
        inc/internal/builtin/json.h \
        inc/internal/builtin/sys.h \
        inc/internal/builtin/uuid.h \
        inc/internal/types/number_types.h \
        inc/internal/types/sequential_types.h \
        inc/internal/types/string.h \
        inc/internal/types/xptr.h \
        inc/lib/buffer.h \
        inc/lib/helpers.h \
        inc/lib/list.h \
        inc/lib/utf8.h \
        tools/gen.h

BUILT_SOURCES= \
        inc/instruction_defs.h \
        inc/token_gen.h \
        src/disassemble_gen.c.h \
        src/tokutils_gen.c \
        src/vm_gen.c.h

.PHONY: headercheck
headercheck: $(noinst_HEADERS) $(BUILT_SOURCES)
	@failed=0; \
        for header in $(noinst_HEADERS); do \
          echo "Checking header $$header"; \
          if ! $(COMPILE) -I$(top_srcdir)/inc -c $$header -o /dev/null; \
            then failed=1; \
          fi; \
        done; \
        exit $$failed

COMMON_LDADD=-lm src/libevilcandy.a
if WANT_READLINE
COMMON_LDADD+=-lreadline
endif
evilcandy_LDADD=$(COMMON_LDADD)
programs_unit_tests_LDADD=$(COMMON_LDADD)
programs_fuzzer_LDADD=$(COMMON_LDADD)

evilcandydir=${datadir}/evilcandy
# well, common except to source code generators
COMMON_CPPFLAGS=\
 -I$(top_srcdir)/inc \
 -DRCDATADIR=\"$(evilcandydir)\" \
 -Wall

src_libevilcandy_a_CPPFLAGS=$(COMMON_CPPFLAGS)
evilcandy_CPPFLAGS=$(COMMON_CPPFLAGS)
programs_unit_tests_CPPFLAGS=$(COMMON_CPPFLAGS)
programs_fuzzer_CPPFLAGS=$(COMMON_CPPFLAGS)

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
nodist_programs_unit_tests_SOURCES = $(nodist_COMMON_SOURCES)
nodist_programs_fuzzer_SOURCES = $(nodist_COMMON_SOURCES)

CLEANFILES = \
        inc/instruction_defs.h \
        inc/token_gen.h \
        src/disassemble_gen.c.h \
        src/tokutils_gen.c \
        src/vm_gen.c.h \
        $(EXTRA_PROGRAMS)

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


