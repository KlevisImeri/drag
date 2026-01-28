#ifndef MACROS_H
#define MACROS_H

#ifdef DEBUG
#define LOG(...) fprintf(stderr, "[DEBUG] " __VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif

#define __DEFER__(func_name, var_name) \
    auto void func_name(int*); \
    int var_name __attribute__((__cleanup__(func_name))); \
    auto void func_name(int*)

#define DEFER_ONE(N) __DEFER__(__DEFER__FUNC ## N, __DEFER__VAR ## N)
#define DEFER_TWO(N) DEFER_ONE(N)
#define defer DEFER_TWO(__COUNTER__)

#endif // MACROS_H
