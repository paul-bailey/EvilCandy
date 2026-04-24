# Do this instead of check_PROGRAMS, since I do not necessarily
# want to run this during 'make check'
.PHONY: tests
tests: programs/unit_tests programs/fuzzer programs/rtfuzzer

man_MANS = etc/evilcandy.1

ACLOCAL_AMFLAGS = -I build/m4
AM_CFLAGS = \
 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -pedantic \
 -Wmissing-prototypes -Wmissing-declarations \
 -Wredundant-decls -Wstrict-prototypes

bin_PROGRAMS = evilcandy
EXTRA_PROGRAMS = programs/unit_tests programs/fuzzer programs/rtfuzzer
check_PROGRAMS = tools/tokgen tools/gen

test_lib = tests/c/libtest.a
# FIXME: this compiles test_lib for default make, even though it's
# only needed for `make tests`
noinst_LIBRARIES = src/libevilcandy.a $(test_lib)

tools_tokgen_SOURCES = tools/tokgen.c tools/genutil.c
tools_gen_SOURCES = tools/gen.c
tools_gen_CPPFLAGS = -I$(top_srcdir)/inc

tests_c_libtest_a_SOURCES = \
        tests/c/strbuf.c \
        tests/c/tap.c

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
        src/types/registry.c \
        src/types/xptr.c

programs_unit_tests_SOURCES = programs/unit_tests.c
programs_fuzzer_SOURCES = programs/fuzzer.c
programs_rtfuzzer_SOURCES = programs/rtfuzzer.c
evilcandy_SOURCES = programs/evilcandy.c

# Note: there's also a fuzzer but it's slow!
TESTS = tests/run-evilcandy-tests.sh \
        tests/regress-binfile-seek-buffer.sh \
        tests/regress-textfile-seek-eof.sh \
        tests/regress-gh-issue-11.sh \
        tests/regress-gh-issue-39-tty.sh \
        programs/unit_tests

noinst_HEADERS = \
        inc/evilcandy/config.h \
        inc/evilcandy/version.h \
        inc/evilcandy/assemble.h \
        inc/evilcandy/strto.h \
        inc/evilcandy/debug.h \
        inc/evilcandy/compiler.h \
        inc/evilcandy/disassemble.h \
        inc/evilcandy/enums.h \
        inc/evilcandy/err.h \
        inc/evilcandy/errmsg.h \
        inc/evilcandy/evc_ctype.h \
        inc/evilcandy/ewrappers.h \
        inc/evilcandy/global.h \
        inc/evilcandy/hash.h \
        inc/evilcandy/myreadline.h \
        inc/evilcandy/typedefs.h \
        inc/evilcandy/var.h \
        inc/evilcandy/vm.h \
        inc/evilcandy/string_reader.h \
        inc/evilcandy/string_writer.h \
        inc/evilcandy/iterator.h \
        inc/evilcandy/types/array.h \
        inc/evilcandy/types/bytes.h \
        inc/evilcandy/types/cell.h \
        inc/evilcandy/types/class.h \
        inc/evilcandy/types/dict.h \
        inc/evilcandy/types/function.h \
        inc/evilcandy/types/generator.h \
        inc/evilcandy/types/method.h \
        inc/evilcandy/types/number_types.h \
        inc/evilcandy/types/property.h \
        inc/evilcandy/types/set.h \
        inc/evilcandy/types/string.h \
        inc/evilcandy/types/tuple.h \
        inc/internal/instructions.h \
        inc/internal/instruction_name.h \
        inc/internal/recursion.h \
        inc/internal/codec.h \
        inc/internal/cwd.h \
        inc/internal/err.h \
        inc/internal/errmsg.h \
        inc/internal/global.h \
        inc/internal/import.h \
        inc/internal/path.h \
        inc/internal/token.h \
        inc/internal/assemble.h \
        inc/internal/init.h \
        inc/internal/op.h \
        inc/internal/type_protocol.h \
        inc/internal/type_registry.h \
        inc/internal/uarg.h \
        inc/internal/vm.h \
        inc/internal/locations.h \
        inc/internal/builtin/io.h \
        inc/internal/builtin/json.h \
        inc/internal/builtin/sys.h \
        inc/internal/builtin/uuid.h \
        inc/internal/types/number_types.h \
        inc/internal/types/internal_types.h \
        inc/internal/types/sequential_types.h \
        inc/internal/types/string.h \
        inc/internal/types/xptr.h \
        inc/lib/buffer.h \
        inc/lib/helpers.h \
        inc/lib/list.h \
        inc/lib/utf8.h \
        inc/tests/strbuf.h \
        inc/tests/tap.h \
        tools/gen.h

BUILT_SOURCES= \
        inc/instruction_defs.h \
        inc/token_gen.h \
        src/disassemble_gen.c.h \
        src/tokutils_gen.c \
        src/vm_gen.c.h \
        inc/evilcandy/build_version.h

.PHONY: FORCE
FORCE:

inc/evilcandy/build_version.h: FORCE
	$(MKDIR_P) inc/evilcandy
	@version=`cd $(top_srcdir) && git describe --tags --dirty --always 2>/dev/null || echo "$(PACKAGE_VERSION)"`; \
	echo '#ifndef EVILCANDY_BUILD_VERSION_H' > $@.tmp; \
	echo '#define EVILCANDY_BUILD_VERSION_H' >> $@.tmp; \
	echo '#define EVILCANDY_BUILD_VERSION "EvilCandy '"$$version"'"' >> $@.tmp; \
	echo '#endif' >> $@.tmp; \
	if test -r $@ && cmp -s $@.tmp $@; then \
	  rm -f $@.tmp; \
	else \
	  mv $@.tmp $@; \
	fi

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

COMMON_LDADD=src/libevilcandy.a -lm
if WANT_READLINE
COMMON_LDADD+=-lreadline
endif
evilcandy_LDADD=$(COMMON_LDADD)
programs_unit_tests_LDADD=$(test_lib) $(COMMON_LDADD)
programs_fuzzer_LDADD=$(test_lib) $(COMMON_LDADD)
programs_rtfuzzer_LDADD=$(test_lib) $(COMMON_LDADD)

evilcandydir=${datadir}/evilcandy
# well, common except to source code generators
COMMON_CPPFLAGS=\
 -I$(top_builddir)/inc \
 -I$(top_builddir)/src \
 -I$(top_srcdir)/inc \
 -DRCDATADIR=\"$(evilcandydir)\" \
 -Wall

src_libevilcandy_a_CPPFLAGS=$(COMMON_CPPFLAGS)
tests_c_libtest_a_CPPFLAGS=$(COMMON_CPPFLAGS)
evilcandy_CPPFLAGS=$(COMMON_CPPFLAGS)
programs_unit_tests_CPPFLAGS=$(COMMON_CPPFLAGS)
programs_fuzzer_CPPFLAGS=$(COMMON_CPPFLAGS)
programs_rtfuzzer_CPPFLAGS=$(COMMON_CPPFLAGS)

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
nodist_programs_rtfuzzer_SOURCES = $(nodist_COMMON_SOURCES)

programs/evilcandy.$(OBJEXT): inc/evilcandy/build_version.h
programs/fuzzer.$(OBJEXT): inc/evilcandy/build_version.h
programs/rtfuzzer.$(OBJEXT): inc/evilcandy/build_version.h

CLEANFILES = \
        inc/instruction_defs.h \
        inc/token_gen.h \
        src/disassemble_gen.c.h \
        src/tokutils_gen.c \
        src/vm_gen.c.h \
        inc/evilcandy/build_version.h \
        inc/evilcandy/build_version.h.tmp \
        $(EXTRA_PROGRAMS)

src/disassemble_gen.c.h: tools/instructions tools/gen
	$(MKDIR_P) src
	tools/gen dis < $(srcdir)/tools/instructions > $@

inc/instruction_defs.h: tools/instructions tools/gen
	$(MKDIR_P) inc
	tools/gen def < $(srcdir)/tools/instructions > $@

inc/token_gen.h: tools/tokens tools/tokgen
	$(MKDIR_P) inc
	tools/tokgen enum < $(srcdir)/tools/tokens > $@

src/tokutils_gen.c: tools/tokens tools/tokgen
	$(MKDIR_P) src
	tools/tokgen util < $(srcdir)/tools/tokens > $@

src/vm_gen.c.h: tools/instructions tools/gen
	$(MKDIR_P) src
	tools/gen jump < $(srcdir)/tools/instructions > $@

evilcandy_demos = \
        demos/text_adventure.evc \
        demos/primes.evc \
        demos/iife.evc \
        demos/dog_cat.evc \
        demos/composition.evc \
        demos/cat.evc \
        demos/socket/client_tcp.evc \
        demos/socket/client_udp.evc \
        demos/socket/server_tcp.evc \
        demos/socket/server_udp.evc \
        demos/socket/realdumbcurl.evc \
        demos/simple_benchmarks/generator.evc \
        demos/simple_benchmarks/sqrt.egq

evilcandy_docs = \
        Documentation/Makefile \
        Documentation/conf.py \
        Documentation/contents.rst \
        Documentation/evc_lexer.py \
        Documentation/make.bat \
        Documentation/_static/css/custom.css \
        Documentation/tutorial/classes_and_namespaces.rst \
        Documentation/tutorial/data_types.rst \
        Documentation/tutorial/getting_started.rst \
        Documentation/tutorial/index.rst \
        Documentation/tutorial/modules_and_imports.rst \
        Documentation/tutorial/program_flow.rst \
        Documentation/tutorial/scope.rst \
        Documentation/langref/data.rst \
        Documentation/langref/filespec.rst \
        Documentation/langref/index.rst \
        Documentation/langref/tokens.rst \
        Documentation/libref/index.rst

# FIXME: string_include.c won't have a depfile if placed here,
# but I can't put it in noinst_HEADERS, because then `make headercheck`
# will fail.
EXTRA_DIST = \
        src/types/string_include.c.h \
        tools/instructions \
        tools/tokens \
        tests/testsuite.evc \
        tests/sanity.evc \
        $(TESTS) \
        $(evilcandy_demos) \
        $(evilcandy_docs) \
        etc/evilcandy.1 \
        source_tree_layout.txt \
        ROADMAP.md \
        AGENTS.md \
        AUTHORS \
        COPYING \
        README

