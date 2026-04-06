#include <evilcandy.h>
#include <iterator.h>

/**
 * iterator_foreach - Perform an action on each member of an iterator.
 * @it:         Iterator to traverse
 * @action:     Callback to perform the action.
 *              The first argument is an element of the target of @it.
 *              The second argument is @data.
 *              If @action returns RES_ERROR, the iteration will stop.
 * @data:       Local data to pass as the second argument of @action.
 *
 * Return: RES_OK if @it fully traversed without error, RES_ERROR
 *         otherwise.
 */
enum result_t
iterator_foreach(Object *it,
                 enum result_t (*action)(Object *, void *),
                 void *data)
{
        Object *child;
        enum result_t res;

        ITERATOR_FOREACH(child, it) {
                res = action(child, data);
                VAR_DECR_REF(child);
                if (res == RES_ERROR) {
                        child = ErrorVar;
                        break;
                }
        }
        return child == ErrorVar ? RES_ERROR : RES_OK;
}

/**
 * iterator_errget - Get iterator or else raise an exception
 * @obj:        Object to get an iterator from
 * @fname:      UAPI function name, if applicable
 *
 * Return: ErrorVar if @obj is not iterable, iterator otherwise.  If
 *         the return value is ErrorVar, an exception has been raised.
 */
Object *
iterator_errget(Object *obj, const char *fname)
{
        Object *it = iterator_get(obj);
        if (!it) {
                it = ErrorVar;
                err_iterable(obj, fname);
        }
        return it;
}


