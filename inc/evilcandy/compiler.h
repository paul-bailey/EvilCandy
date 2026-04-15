#ifndef EVC_COMPILER_H
#define EVC_COMPILER_H

#if defined(__cplusplus) && __cplusplus >= 201703L
    #define WARN_UNUSED_RESULT [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
    #define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#elif defined(_MSC_VER) && _MSC_VER >= 1700
    #define WARN_UNUSED_RESULT _Check_return_
#else
    #define WARN_UNUSED_RESULT
#endif

#endif /* EVC_COMPILER_H */
