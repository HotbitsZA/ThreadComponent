#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "simple_container_V2.h"

int main()
{
  simple_container_V2<std::string, std::vector> items;
  items.reserve(8);

  std::vector<std::string> firstBatch{"alpha", "beta", "gamma"};
  items.insert(std::move(firstBatch));
  items.insert(std::string("delta"));

  const auto snapshot = items.snapshot();
  std::cout << "Snapshot size: " << snapshot.size() << std::endl;

  auto grabbed = items.grab(
      2,
      [](const std::string &value)
      {
        return value < "delta"
                   ? simple_container_V2<std::string, std::vector>::enm_Select::grab
                   : simple_container_V2<std::string, std::vector>::enm_Select::skip;
      },
      0);

  std::cout << "Predicate grab result:" << std::endl;
  for (const auto &value : grabbed)
    std::cout << "  " << value << std::endl;

  std::thread delayedProducer(
      [&items]()
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        items.insert(std::string("epsilon"));
      });

  auto timedGrab = items.grab(10, std::chrono::milliseconds(500));
  delayedProducer.join();

  std::cout << "Timed grab result:" << std::endl;
  for (const auto &value : timedGrab)
    std::cout << "  " << value << std::endl;

  return 0;
}
