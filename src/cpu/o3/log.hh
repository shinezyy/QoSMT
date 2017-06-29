#ifndef __ZYY_LOG__
#define __ZYY_LOG__

#include <string>


#define log2(format,...) \
    printf("%s,%s,%d: " format, \
            __FILE__, __func__, __LINE__, ## __VA_ARGS__), \
            fflush(stdout) \


#define log_var(x) \
    printf("%s,%s,%d: " "%s is %d\n", \
            __FILE__, __func__, __LINE__, #x, x), \
            fflush(stdout) \


#define log_var_cond(b, x) \
    do {\
        if (b) { \
            printf("%s,%s,%d: " "%s is %d\n", \
                    __FILE__, __func__, __LINE__, #x, x), \
                fflush(stdout); \
        }\
    } while(0)

template <class T>
std::string dis(T x) {
    std::string s = "N/A";
    return x ? (x)->staticInst->disassemble((x)->instAddr()) : s;
}

#endif
