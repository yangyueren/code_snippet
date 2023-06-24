#include <algorithm>
#include <array>
#include <iostream>
#include <cstdio>
#include <type_traits>
#include <typeinfo>
#include "print.h"


template<class ...Ts>
auto func(Ts ...ts){
    // return sizeof...(ts);
    using T = std::common_type_t<Ts...>;
    printnl("{");
    ((printnl(", "), printnl(ts)), ...);
    printnl("}\n");
    return std::array<T, sizeof...(ts)>{ts...};
}



// implement common_type


template<class T1, class T2>
struct common_type_two{
    // here using 0?x:y to find common type
    using type = decltype(0 ? std::declval<T1>() : std::declval<T2>());
};

template <class ...Ts>
struct my_common_type{};

template <class T0>
struct my_common_type<T0>{
    using type = T0;
};
template <class T0, class T1, class ...Ts>
struct my_common_type<T0, T1, Ts...>{
   using type = typename common_type_two<T0, typename my_common_type<T1, Ts...>::type>::type; 
};

// using constexpr to impl common_type

template<class T>
struct dummy{
    static constexpr T declval(){
        return std::declval<T>();
    }
}; // dummy is to avoid T0's move/copy assignment is deleted.
template <class T0, class ...Ts>
constexpr auto get_common_type(dummy<T0> t0, dummy<Ts> ...ts) {
    if constexpr(sizeof...(Ts) == 0){
        return dummy<T0>{};
    } else {
        return dummy<decltype(0 ? t0.declval() : get_common_type(ts...).declval())>{};
    }
}
void test_my_common_type(){
    std::cout << "test_my_common_type" << std::endl;
    using what = typename my_common_type<int, double, float>::type;
    std::cout << typeid(what).name() << std::endl;
    using what = decltype(get_common_type(dummy<int>{}, dummy<double>{}).declval());
    std::cout << typeid(what).name() << std::endl;


}

int main(){
    test_my_common_type();
    return 0;
}
