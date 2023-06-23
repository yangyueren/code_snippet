#include <iostream>
#include <type_traits>
#include <vector>
using namespace std;
template <typename T> struct has_reserve {

  struct good {
    char dummy[2];
  };
  struct bad {};
  template <typename U, void (U::*)(size_t)> struct SFINAE {};

  template <typename U> static good reserve(SFINAE<U, &U::reserve> *);
  template <typename U> static bad reserve(...);

  static const bool value = sizeof(reserve<T>(nullptr)) == sizeof(good);
};

void test_has_reserve() {
  std::cout << "vector has_reserve: " << has_reserve<vector<int>>::value
            << std::endl;
  std::cout << "int has_reserve: " << has_reserve<int>::value << std::endl;
}

template <typename C, typename T>
std::enable_if_t<has_reserve<C>::value, void> append(C &container, T *ptr,
                                                     size_t size) {
  container.reserve(container.size() + size);
  // std::cout << "yyyyy" << std::endl;
  for (size_t i = 0; i < size; i++) {
    container.push_back(ptr[i]);
  }
}
template <typename C, typename T>
std::enable_if_t<!has_reserve<C>::value, void> append(C &container, T *ptr,
                                                      size_t size) {
  std::cout << "yyyyy !has_reserve" << std::endl;
  for (size_t i = 0; i < size; i++) {
    // container.push_back(ptr[i]);
  }
}

void test_append() {
  // std::vector<int> v;
  int v;
  int p[5] = {0, 1, 2, 3, 4};
  append(v, p, 5);
  // std::cout << "append v.size " << v.size() << std::endl;
}

template <typename C, typename T>
auto decl_append(C &container, T *ptr, size_t size)
    -> decltype(declval<C &>().reserve(1U), void()) {
  std::cout << "yyyyy decl_append has reserve" << std::endl;
  container.reserve(container.size() + size);
  for (size_t i = 0; i < size; i++) {
    container.push_back(ptr[i]);
  }
}

void test_decl_append() {
  std::vector<int> v;
  // int v;
  int p[5] = {0, 1, 2, 3, 4};
  decl_append(v, p, 5);
  std::cout << "append v.size " << v.size() << std::endl;
}

template <typename T, typename = void_t<>>
struct decl_has_reserve : std::false_type {};

template <typename T>
struct decl_has_reserve<T, void_t<decltype(declval<T &>().reserve(1U))>>
    : std::true_type {};

template <typename C, typename T>
void _append(C &container, T *ptr, size_t size, std::true_type) {
  container.reserve(container.size() + size);
  for (size_t i = 0; i < size; i++) {
    container.push_back(ptr[i]);
  }
}
template <typename C, typename T>
void _append(C &container, T *ptr, size_t size, std::false_type) {
  for (size_t i = 0; i < size; i++) {
    container.push_back(ptr[i]);
  }
}

template <typename C, typename T>
void tag_dispatch_append(C &container, T *ptr, size_t size) {
  // _append(container, ptr, size,
  //         std::integral_constant<bool, decl_has_reserve<C>::value>{});

  // they are same.
  _append(container, ptr, size, decl_has_reserve<C>{});
          
}

void test_tag_dispatch_append() {
  std::cout << "test_tag_dispatch_append" << std::endl;
  std::vector<int> v;
  // int v;
  int p[5] = {0, 1, 2, 3, 4};
  tag_dispatch_append(v, p, 5);
  std::cout << "append v.size " << v.size() << std::endl;
}

int main() {
  test_has_reserve();
  test_append();
  test_decl_append();
  test_tag_dispatch_append();
  return 0;
}
