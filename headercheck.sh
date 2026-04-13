headers="compiler.h
debug.h
evcenums.h
evilcandy.h
global.h
hash.h
instructions.h
iterator.h
recursion.h
string_reader.h
string_writer.h
typedefs.h
uarg.h
var.h
vm.h
lib/buffer.h
lib/helpers.h
lib/list.h
lib/utf8.h"

for i in $headers
do
    printf "#include <$i>\nint main(void){return 0;}\n" \
        | cc -I. -Iinc -c -x c -o /tmp/evc-headercheck.o -
done

headers="
token.h
assemble.h
init.h
op.h
type_protocol.h
type_registry.h
vm.h
locations.h"

for i in $headers
do
    printf "#include <internal/$i>\nint main(void){return 0;}\n" \
        | cc -I. -Iinc -c -x c -o /tmp/evc-headercheck.o -
done

headers="
io.h
sys.h"

for i in $headers
do
    printf "#include <internal/builtin/$i>\nint main(void){return 0;}\n" \
        | cc -I. -Iinc -c -x c -o /tmp/evc-headercheck.o -
done

headers="
number_types.h
sequential_types.h
string.h
xptr.h"

for i in $headers
do
    printf "#include <internal/types/$i>\nint main(void){return 0;}\n" \
        | cc -I. -Iinc -c -x c -o /tmp/evc-headercheck.o -
done


