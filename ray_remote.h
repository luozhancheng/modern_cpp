

#pragma once

#include <iostream>
#include <vector>
#include <functional>
#include <unordered_map>
#include <msgpack.hpp>
#include <boost/callable_traits.hpp>
#include <boost/optional.hpp>

template<typename>
struct RemoveReference;

template<class... T>
struct RemoveReference<std::tuple<T...>> {
    using type = std::tuple<std::remove_const_t<std::remove_reference_t<T>>...>;
};

template<class Tuple>
using RemoveReference_t = typename RemoveReference<Tuple>::type;



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

RemoteFunction *GetFunction(const std::string &func_name) {
    auto it = map_invokers.find(func_name);
    if (it == map_invokers.end()) {
        return nullptr;
    }

    return &it->second;
}

template<typename Function>
std::enable_if_t<!std::is_member_function_pointer<Function>::value, std::string>
GetFunctionName(const Function &f) {
    auto it = func_to_key_map.find(GetAddress(f));
    if (it == func_to_key_map.end()) {
        return "";
    }

    return it->second;
}

struct RemoteFunctionHolder {
    RemoteFunctionHolder() = default;

    template<typename F>
    RemoteFunctionHolder(F func) {
        auto func_name = GetFunctionName(func);
        if (func_name.empty()) {
            std::cout << "func name is empty !!!!!" << std::endl;
        }
        function_name = std::move(func_name);
    }

    /// The remote function name.
    std::string function_name;
};

struct TaskArg {
    TaskArg() = default;

    TaskArg(TaskArg &&rhs) {
        buf = std::move(rhs.buf);
        id = rhs.id;
    }

    TaskArg(const TaskArg &) = delete;

    TaskArg &operator=(TaskArg const &) = delete;

    TaskArg &operator=(TaskArg &&) = delete;

    /// If the buf is initialized shows it is a value argument.
    boost::optional<msgpack::sbuffer> buf;
    /// If the id is initialized shows it is a reference argument.
    boost::optional<std::string> id;
};

class Serializer {
public:
    template<typename T>
    static msgpack::sbuffer Serialize(const T &t) {
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, t);
        return buffer;
    }

    template<typename T>
    static T Deserialize(const char *data, size_t size) {
        msgpack::unpacked unpacked;
        msgpack::unpack(unpacked, data, size);
        return unpacked.get().as<T>();
    }

    template <typename T>
    static std::pair<bool, T> DeserializeWhenNil(const char *data, size_t size) {
        T val;
        size_t off = 0;
        msgpack::unpacked unpacked = msgpack::unpack(data, size, off);
        if (!unpacked.get().convert_if_not_nil(val)) {
            return {false, {}};
        }

        return {true, val};
    }
};

template<typename T>
inline static std::enable_if_t<!std::is_pointer<T>::value, msgpack::sbuffer>
PackReturnValue(T result) {
    return Serializer::Serialize(std::move(result));
}

class Arguments {
public:
    template<typename ArgType>
    static void WrapArgsImpl(std::vector<TaskArg> *task_args, ArgType &&arg) {

        msgpack::sbuffer buffer = Serializer::Serialize(arg);
        TaskArg task_arg;
        task_arg.buf = std::move(buffer);
        /// Pass by value.
        task_args->emplace_back(std::move(task_arg));
    }

    template<typename... OtherArgTypes>
    static void WrapArgs(std::vector<TaskArg> *task_args,
                         OtherArgTypes &&... args) {
        (void) std::initializer_list<int>{
                (WrapArgsImpl(task_args, std::forward<OtherArgTypes>(args)), 0)...};
        /// Silence gcc warning error.
        (void) task_args;
    }
};

template<typename T>
static inline T ParseArg(char *data, size_t size, bool &is_ok) {
    auto pair = Serializer::DeserializeWhenNil<T>(data, size);
    is_ok = pair.first;
    return pair.second;
}

template<size_t... I, typename... Args>
static inline bool GetArgsTuple(std::tuple<Args...> &tp,
                                const std::vector<msgpack::sbuffer> &args_buffer,
                                std::index_sequence<I...>) {
    bool is_ok = true;
    (void) std::initializer_list<int>{
            (std::get<I>(tp) = ParseArg<Args>((char *) args_buffer.at(I).data(),
                                              args_buffer.at(I).size(), is_ok),
                    0)...};
    return is_ok;
}

template<typename F>
class TaskCaller {
public:
    TaskCaller();

    TaskCaller(RemoteFunctionHolder remote_function_holder) :
            remote_function_holder_(std::move(remote_function_holder)) {

    }

    auto Call(const RemoteFunctionHolder &remote_function_holder,
              std::vector<TaskArg> &args) {
        std::cout << "call function : " << remote_function_holder.function_name << std::endl;
        using ReturnType = boost::callable_traits::return_type_t<F>;
        auto f = GetFunction(remote_function_holder.function_name);
        std::vector<msgpack::sbuffer> args_buffer;
        for (size_t i = 0; i < args.size(); i++) {
            args_buffer.push_back(std::move(args[i].buf.get()));
        }

        auto ret_buf = (*f)(args_buffer);
        ReturnType a = Serializer::Deserialize<ReturnType>(ret_buf.data(), ret_buf.size());
        return a;
    }

    template<typename... Args>
    boost::callable_traits::return_type_t<F> Remote(Args &&... args) {
        using ReturnType = boost::callable_traits::return_type_t<F>;
        Arguments::WrapArgs(&args_, std::forward<Args>(args)...);
        return Call(remote_function_holder_, args_);
    }

private:
    RemoteFunctionHolder remote_function_holder_{};
    std::vector<TaskArg> args_;
};

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

template<typename R, typename F, size_t... I, typename... Args>
static R CallInternal(const F &f, const std::index_sequence<I...> &,
                      std::tuple<Args...> args) {
    (void) args;
    using ArgsTuple = boost::callable_traits::args_t<F>;
    return f(((typename std::tuple_element<I, ArgsTuple>::type) std::get<I>(args))...);
}

template<typename R, typename F, typename... Args>
static std::enable_if_t<!std::is_void<R>::value, msgpack::sbuffer> Call(
        const F &f, std::tuple<Args...> args) {
    auto r =
            CallInternal<R>(f, std::make_index_sequence<sizeof...(Args)>{}, std::move(args));
    return PackReturnValue(r);
}


template <typename R, typename F, typename... Args>
static std::enable_if_t<std::is_void<R>::value, msgpack::sbuffer> Call(
        const F &f, std::tuple<Args...> args) {
    CallInternal<R>(f, std::make_index_sequence<sizeof...(Args)>{}, std::move(args));
    return Serializer::Serialize(msgpack::type::nil_t());
}

template<typename Function>
static inline msgpack::sbuffer Apply(const Function &func,
                                     const std::vector<msgpack::sbuffer> &args_buffer) {
    auto func_name = GetFunctionName(func);
    std::cout << "Apply a function: " << func_name << std::endl;

    using RetrunType = boost::callable_traits::return_type_t<Function>;
    using ArgsTuple = RemoveReference_t<boost::callable_traits::args_t<Function>>;

    msgpack::sbuffer result;
    ArgsTuple tp{};
    bool is_ok = GetArgsTuple(
            tp, args_buffer, std::make_index_sequence<std::tuple_size<ArgsTuple>::value>{});

    result = Call<RetrunType>(func, std::move(tp));
    return result;
}

template<typename Function>
std::enable_if_t<!std::is_member_function_pointer<Function>::value, bool>
RegisterRF(std::string const &name, const Function &f) {
    std::cout << name << std::endl;
    auto pair = func_to_key_map.template emplace(GetAddress(f), name);
    map_invokers.template emplace(name,
                                  std::bind(&Apply<Function>, std::move(f),
                                            std::placeholders::_1));
    return true;
}

template<typename T, typename... U>
inline static int RegisterRF(const T &t, U... u) {
    std::cout << t << std::endl;
    int index = 0;
    const auto func_names = GetFunctionNames(t);
    (void) std::initializer_list<int>{
            (RegisterRF(std::string(func_names[index].data(),
                                    func_names[index].length()), u),
                    index++, 0)...};
    return 0;
}

#define REMOTE(...) \
static auto ANONYMOUS_VARIABLE(var) = \
    RegisterRF(#__VA_ARGS__, __VA_ARGS__);

/// Normal task.
template<typename F>
TaskCaller<F> Task(F func) {
    RemoteFunctionHolder remote_func_holder(func);
    return TaskCaller<F>(std::move(remote_func_holder));
}

/*********************************************************/
// test

void add() {

}

int sub(int a, int b) {
    return a - b;
}

REMOTE(add, sub)

namespace ray_remote {
    class Test {
    public:
        Test() {
            std::cout << "testing ray remote!" << std::endl;
            auto result = Task(sub).Remote(3, 1);
            std::cout << "result = " << result << std::endl;
        }
    };

    Test test;
}

