#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
  std::vector<int*> integers{};
  std::cout << "Press enter when ready.\n";
  std::cin.get();
  for (int i = 0; i < 1000; ++i)
  {
    integers.push_back(reinterpret_cast<int*>(malloc(sizeof(int))));
  }

  std::cout << "Memory has been allocated. Press enter to release it.\n";
  std::cin.get();

  for (int* i : integers)
  {
    free(i);
  }

  std::cout << "Memory has been released. Press enter to exit.\n";
  std::cin.get();
  return 0;
}
