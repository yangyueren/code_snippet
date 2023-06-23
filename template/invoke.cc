#include <iostream>
#include <type_traits>

template <typename F> auto invoke_expr(F f) {
  if constexpr (std::is_same_v<std::invoke_result_t<F>, void>) {
    std::cout << "invoke expr void" << std::endl;
    f();
    std::cout << "invoke expr void end" << std::endl;
  } else {
    std::cout << "invoke expr void" << std::endl;
    auto ret = f();
    std::cout << "invoke expr void end" << std::endl;
    return ret;
  }
}

void test_invoke_constexpr() {
  invoke_expr([]() -> void { std::cout << "call void"; });
  std::cout << invoke_expr([]() -> int {
    std::cout << "call int";
    return 802;
  }) << std::endl;
}

// below two invoke_sfinae must be disjoin, otherwise redefinition
template <typename F,
          std::enable_if_t<std::is_void_v<std::invoke_result_t<F>>, int> = 0>
auto invoke_sfinae(F f) {
  std::cout << "enter void" << std::endl;
  f();
}
template <typename F,
          std::enable_if_t<!std::is_void_v<std::invoke_result_t<F>>, int> = 0>
auto invoke_sfinae(F f) {
  std::cout << "enter non-void" << std::endl;
  auto ret = f();
  return ret;
}

void test_invoke_sfinae() {
  invoke_sfinae([]() -> void { std::cout << "call void"; });
  std::cout << invoke_sfinae([]() -> int {
    std::cout << "call int";
    return 802;
  }) << std::endl;
}

int main() {
  test_invoke_constexpr();
  test_invoke_sfinae();
  return 0;
}
