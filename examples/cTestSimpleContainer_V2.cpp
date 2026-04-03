#include "../simple_container_V2.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <thread>
#include <vector>

#define CATCH_CONFIG_MAIN
#include "../../../OpenSource/CatchTestingFramework/catch-1.8.2/catch.hpp"

TEST_CASE("V2 single insert and grab across base containers", "[simple_cntr_v2]")
{
  simple_container_V2<int> items1;
  simple_container_V2<int, std::list> items2;
  simple_container_V2<int, std::vector> items3;

  items1.insert(1);
  items1.insert(2);

  items2.insert(1);
  items2.insert(2);

  items3.insert(1);
  items3.insert(2);

  REQUIRE(items1.size() == 2);
  REQUIRE(items2.size() == 2);
  REQUIRE(items3.size() == 2);

  auto grab1 = items1.grab(10);
  auto grab2 = items2.grab(10);
  auto grab3 = items3.grab(10);

  REQUIRE(items1.empty());
  REQUIRE(items2.empty());
  REQUIRE(items3.empty());

  REQUIRE(grab1.size() == 2);
  REQUIRE(grab2.size() == 2);
  REQUIRE(grab3.size() == 2);

  REQUIRE(grab1[0] == 1);
  REQUIRE(grab1[1] == 2);

  auto listIt = grab2.begin();
  REQUIRE(*listIt == 1);
  ++listIt;
  REQUIRE(*listIt == 2);

  REQUIRE(grab3[0] == 1);
  REQUIRE(grab3[1] == 2);
}

TEST_CASE("V2 batch insert supports move and copy inputs", "[simple_cntr_v2]")
{
  simple_container_V2<int, std::vector> items;
  items.reserve(8);

  std::vector<int> moved{1, 2, 3};
  items.insert(std::move(moved));

  REQUIRE(moved.empty());
  REQUIRE(items.size() == 3);

  const std::vector<int> copied{4, 5};
  items.insert(copied);

  REQUIRE(copied.size() == 2);
  REQUIRE(items.size() == 5);

  auto all = items.grab(10);
  REQUIRE(all.size() == 5);
  REQUIRE(all[0] == 1);
  REQUIRE(all[1] == 2);
  REQUIRE(all[2] == 3);
  REQUIRE(all[3] == 4);
  REQUIRE(all[4] == 5);
}

TEST_CASE("V2 timed grab waits and returns empty when no items arrive", "[simple_cntr_v2]")
{
  simple_container_V2<int> items;

  const auto start = std::chrono::steady_clock::now();
  auto result = items.grab(1, std::chrono::milliseconds(20));
  const auto elapsed = std::chrono::steady_clock::now() - start;

  REQUIRE(result.empty());
  REQUIRE(elapsed >= std::chrono::milliseconds(10));
}

TEST_CASE("V2 timed grab unblocks when producer inserts", "[simple_cntr_v2]")
{
  simple_container_V2<int> items;

  std::thread producer([&items]()
                       {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    items.insert(99); });

  auto result = items.grab(1, std::chrono::milliseconds(200));

  producer.join();

  REQUIRE(result.size() == 1);
  REQUIRE(result.front() == 99);
  REQUIRE(items.empty());
}

TEST_CASE("V2 snapshot is non-destructive and supports offsets", "[simple_cntr_v2]")
{
  simple_container_V2<std::string, std::vector> items;
  items.insert(std::string("alpha"));
  items.insert(std::string("beta"));
  items.insert(std::string("gamma"));

  auto snapshot = items.snapshot(2, 1);

  REQUIRE(items.size() == 3);
  REQUIRE(snapshot.size() == 2);
  REQUIRE(snapshot[0] == "beta");
  REQUIRE(snapshot[1] == "gamma");
}

TEST_CASE("V2 predicate grab honors stop_and_requeue without deadlocking", "[simple_cntr_v2]")
{
  simple_container_V2<int, std::vector> items;
  std::vector<int> values{1, 2, 3, 4};
  items.insert(std::move(values));

  auto result = items.grab(
      10,
      [](const int &value)
      {
        if (value <= 2)
        {
          return simple_container_V2<int, std::vector>::enm_Select::grab;
        }

        if (value == 3)
        {
          return simple_container_V2<int, std::vector>::enm_Select::stop_and_requeue;
        }

        return simple_container_V2<int, std::vector>::enm_Select::skip;
      },
      0);

  REQUIRE(result.empty());
  REQUIRE(items.size() == 4);

  auto reordered = items.snapshot();
  REQUIRE(reordered.size() == 4);
  REQUIRE(reordered[0] == 3);
  REQUIRE(reordered[1] == 4);
  REQUIRE(reordered[2] == 1);
  REQUIRE(reordered[3] == 2);
}

TEST_CASE("V2 predicate grab supports stop and offset selection", "[simple_cntr_v2]")
{
  simple_container_V2<int> items;
  items.insert(1);
  items.insert(2);
  items.insert(3);
  items.insert(4);
  items.insert(5);

  auto result = items.grab(
      10,
      [](const int &value)
      {
        if ((value % 2) == 0)
        {
          return simple_container_V2<int>::enm_Select::grab;
        }

        if (value == 5)
        {
          return simple_container_V2<int>::enm_Select::stop;
        }

        return simple_container_V2<int>::enm_Select::skip;
      },
      1);

  REQUIRE(result.size() == 2);
  REQUIRE(result[0] == 2);
  REQUIRE(result[1] == 4);

  auto remaining = items.grab(10);
  REQUIRE(remaining.size() == 3);
  REQUIRE(remaining[0] == 1);
  REQUIRE(remaining[1] == 3);
  REQUIRE(remaining[2] == 5);
}
