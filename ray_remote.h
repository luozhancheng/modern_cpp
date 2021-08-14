#pragma once

#define CONCATENATE_DIRECT(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_DIRECT(s1, s2)
#define ANONYMOUS_VARIABLE(str) CONCATENATE(str, __LINE__)

template <typename T, typename... U>
inline static int RegisterRF(const T &t, U... u) {
    std::cout << t << std::endl;
    return 0;
}

#define REMOTE(...) \
static auto ANONYMOUS_VARIABLE(var) = \
    RegisterRF(#__VA_ARGS__, __VA_ARGS__);



/*********************************************************/
// test

void add() {

}

void sub(int a) {

}

REMOTE(add, sub)