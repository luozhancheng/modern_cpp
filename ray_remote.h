

#pragma once

#include <iostream>
#include <vector>
#include <functional>
#include <unordered_map>
#include <msgpack.hpp>
#include <boost/callable_traits.hpp>

template<class Dest, class Source>
Dest BitCast(const Source &source) {
    static_assert(sizeof(Dest) == sizeof(Source),
                  "BitCast requires source and destination to be the same size");

    Dest dest;
    memcpy(&dest, &source, sizeof(dest));
    return dest;
}

template<typename F>
std::string GetAddress(F f) {
    auto arr = BitCast<std::array<char, sizeof(F)>>(f);
    return std::string(arr.data(), arr.size());
}

using RemoteFunction =
        std::function<msgpack::sbuffer(const std::vector<msgpack::sbuffer> &)>;
using RemoteFunctionMap_t = std::unordered_map<std::string, RemoteFunction>;

RemoteFunctionMap_t map_invokers;
std::unordered_map<std::string, std::string> func_to_key_map;

#define CONCATENATE_DIRECT(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_DIRECT(s1, s2)
#define ANONYMOUS_VARIABLE(str) CONCATENATE(str, __LINE__)

inline static std::vector<std::string_view> GetFunctionNames(std::string_view str) {
    std::vector<std::string_view> output;
    size_t first = 0;

    while (first < str.size()) {
        auto second = str.find_first_of(",", first);

        if (first != second) {
            auto sub_str = str.substr(first, second - first);
            if (sub_str.find_first_of('(') != std::string_view::npos) {
                second = str.find_first_of(")", first) + 1;
            }
            if (str[first] == ' ') {
                first++;
            }

            auto name = str.substr(first, second - first);
            if (name.back() == ' ') {
                name.remove_suffix(1);
            }
            output.emplace_back(name);
        }

        if (second == std::string_view::npos) break;

        first = second + 1;
    }

    return output;
}

template <typename Function>
static inline msgpack::sbuffer Apply(const Function &func,
                                     const std::vector<msgpack::sbuffer> &args_buffer) {

    msgpack::sbuffer result;
    return result;
}

template <typename Function>
std::enable_if_t<!std::is_member_function_pointer<Function>::value, bool>
RegisterRF(std::string const &name, const Function &f) {
    std::cout << name << std::endl;
    auto pair = func_to_key_map.template emplace(GetAddress(f),name);
    map_invokers.template emplace(name,
            std::bind(&Apply<Function>, std::move(f),
                      std::placeholders::_1));
    return true;
}

template <typename T, typename... U>
inline static int RegisterRF(const T &t, U... u) {
    std::cout << t << std::endl;
    int index = 0;
    const auto func_names = GetFunctionNames(t);
    (void)std::initializer_list<int>{
        (RegisterRF(std::string(func_names[index].data(),
                                   func_names[index].length()), u),
                                           index++, 0)...};
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