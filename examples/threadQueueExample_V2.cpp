#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "cThreadQueue_V2.h"

int main()
{
  cThreadQueue_V2<std::string> queue;

  std::thread producer(
      [&queue]()
      {
        for (int i = 1; i <= 5; ++i)
        {
          queue.push("message-" + std::to_string(i));
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        queue.push("done");
      });

  while (true)
  {
    auto item = queue.tryGetFront();
    if (!item.has_value())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      continue;
    }

    std::cout << "Consumed: " << *item << std::endl;
    if (*item == "done")
      break;
  }

  producer.join();
  std::cout << "Queue empty: " << std::boolalpha << queue.empty() << std::endl;
  return 0;
}
