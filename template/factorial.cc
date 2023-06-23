#include <iostream>

template <int n> struct factorial {
  static_assert(n >= 0, "args must >= 0");
  static const int value = n * factorial<n - 1>::value;
};

template <> struct factorial<0> {
  static const int value = 1;
};

int main() {
  int a = factorial<2>::value;
  std::cout << a << std::endl;
  return 0;
}
