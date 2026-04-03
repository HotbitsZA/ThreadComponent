#include "../simple_container.h"
#include "../../BinaryHelper/cDataPacket.h"
#include <iostream>
#include <stdint.h>

#include <vector>
#include <queue>
#include <list>

#define CATCH_CONFIG_MAIN
#include "../../../OpenSource/CatchTestingFramework/catch-1.8.2/catch.hpp"

TEST_CASE("Simple int assign - single", "[simple_cntr]")
{
  simple_container <int > items1;
  simple_container <int, std::list > items2;
  simple_container <int, std::vector > items3;

  //--- single additions
  items1.insert(1);
  items2.insert(1);
  items3.insert(1);

  REQUIRE(items1.size() == 1);
  REQUIRE(items2.size() == 1);
  REQUIRE(items3.size() == 1);

  items1.insert(2);
  items2.insert(2);
  items3.insert(2);

  REQUIRE(items1.size() == 2);
  REQUIRE(items2.size() == 2);
  REQUIRE(items3.size() == 2);

  //-- 
  auto grab1 = items1.grab(100);
  auto grab2 = items2.grab(100);
  auto grab3 = items3.grab(100);

  REQUIRE(items1.size() == 0);
  REQUIRE(items2.size() == 0);
  REQUIRE(items3.size() == 0);

  REQUIRE(grab1.size() == 2);
  REQUIRE(grab2.size() == 2);
  REQUIRE(grab3.size() == 2);

  auto ittr2 = grab2.begin();
  for (int i = 0; i < 2; ++i)
  {
    REQUIRE(grab1.at(i) == i + 1);
    REQUIRE(grab3.at(i) == i + 1);

    REQUIRE(*ittr2 == i + 1);
    std::advance(ittr2, 1);
  }
}

TEST_CASE("Simple int assign - batch", "[simple_cntr]")
{
  simple_container <int > items1;
  simple_container <int, std::list > items2;
  simple_container <int, std::vector > items3;

  //--- batch additions
  std::deque<int> add1;
  std::list<int>  add2;
  std::vector<int> add3;

  for (int i = 0; i < 5; ++i)
  {
    add1.push_back(i + 1);
    add2.push_back(i + 1);
    add3.push_back(i + 1);
  }

  items1.insert(&add1);
  items2.insert(&add2);
  items3.insert(&add3);

  REQUIRE(add1.size() == 0);
  REQUIRE(add2.size() == 0);
  REQUIRE(add3.size() == 0);

  REQUIRE(items1.size() == 5);
  REQUIRE(items2.size() == 5);
  REQUIRE(items3.size() == 5);

  //--
  auto grab1 = items1.grab(100);
  auto grab2 = items2.grab(100);
  auto grab3 = items3.grab(100);

  REQUIRE(items1.size() == 0);
  REQUIRE(items2.size() == 0);
  REQUIRE(items3.size() == 0);

  REQUIRE(grab1.size() == 5);
  REQUIRE(grab2.size() == 5);
  REQUIRE(grab3.size() == 5);
}

TEST_CASE("cDataPacket assign - single", "[simple_cntr]")
{
  simple_container <cDataPacket, std::list > items1;
  simple_container <cDataPacket, std::vector > items2;

  //--- single additions
  {
    const int dataSize = 8;
    const uint8_t data[dataSize] = { 1, 2, 3, 4, 165, 166, 167, 168 };

    items1.insert(std::move(cDataPacket(data, dataSize)));
    items2.insert(std::move(cDataPacket(data, dataSize)));
  }

  REQUIRE(items1.size() == 1);
  REQUIRE(items2.size() == 1);

  {
    const int dataSize = 4;
    const uint8_t data[dataSize] = { 199, 198, 6, 5 };

    items1.insert(std::move(cDataPacket(data, dataSize)));
    items2.insert(std::move(cDataPacket(data, dataSize)));
  }
  

  REQUIRE(items1.size() == 2);
  REQUIRE(items2.size() == 2);

  {
    //-- 
    auto grab1 = items1.grab(2);
    auto grab2 = items2.grab(2);

    REQUIRE(items1.size() == 0);
    REQUIRE(items2.size() == 0);
    REQUIRE(grab1.size()  == 2);
    REQUIRE(grab2.size()  == 2);
    
    // first container compare
    {
      auto ittr1 = grab1.begin();
      REQUIRE(ittr1->GetDataSize() == 8);

      auto pData = ittr1->GetData();

      REQUIRE(pData[0] == 1);
    }
  }
}

TEST_CASE("Container expose - 1", "[simple_cntr]")
{
  using tdDef = simple_container <int, std::deque >;

  tdDef items;

  //
  {
    tdDef::td_BaseContainer add;

    add.push_back(1);
    add.push_back(2);

    items.insert(&add);
  }

  REQUIRE(items.size() == 2);
}

TEST_CASE("BASE insert location FRONT", "[simple_cntr]")
{
  simple_container < int > items;

  {
    //--- single additions
    items.insert(1);
    REQUIRE(items.size() == 1);

    items.insert(2);
    REQUIRE(items.size() == 2);
  }

  //content is [1][2]

  {
    simple_container < int >::td_BaseContainer base;

    for (int i = 0; i < 3; ++i)
    {
      base.push_back(i + 3);
    }

    items.insert(&base, false);
    REQUIRE(items.size() == 5);
  }

  //content is [3][4][5][1][2]
  
  {
    auto grb = items.grab_pointers(5, [](int* value)
    {
      //grab everything
      return simple_container_select::grab;
    }, 0);
    REQUIRE(items.size() == 5);

    int value = 3;
    for (auto& pntr : grb)
    {
      REQUIRE((*pntr) == value);

      value++;
      if (value > 5) value = 1;
    }
  }
}

TEST_CASE("BASE insert location BACK", "[simple_cntr]")
{
  simple_container < int > items;

  {
    //--- single additions
    items.insert(1);
    REQUIRE(items.size() == 1);

    items.insert(2);
    REQUIRE(items.size() == 2);
  }

  //content is [1][2]

  {
    simple_container < int >::td_BaseContainer base;

    for (int i = 0; i < 3; ++i)
    {
      base.push_back(i + 3);
    }

    items.insert(&base);
    REQUIRE(items.size() == 5);
  }

  //content is [1][2][3][4][5]

  {
    auto grb = items.grab(5);
    REQUIRE(items.size() == 0);

    int value = 1;
    for (auto& obj : grb)
    {
      REQUIRE(obj == value);
      value++;
    }
  }
}

TEST_CASE("VECTOR insert location FRONT", "[simple_cntr]")
{
  simple_container < int, std::vector > items;

  {
    //--- single additions
    items.insert(1);
    REQUIRE(items.size() == 1);

    items.insert(2);
    REQUIRE(items.size() == 2);
  }

  //content is [1][2]

  {
    simple_container < int, std::vector >::td_BaseContainer base;

    for (int i = 0; i < 3; ++i)
    {
      base.push_back(i + 3);
    }

    items.insert(&base, false);
    REQUIRE(items.size() == 5);
  }

  //content is [3][4][5][1][2]

  {
    auto grb = items.grab_pointers(5, [](int* value)
    {
      //grab everything
      return simple_container_select::grab;
    }, 0);
    REQUIRE(items.size() == 5);

    REQUIRE((*grb[0]) == 3);
    REQUIRE((*grb[1]) == 4);
    REQUIRE((*grb[2]) == 5);
    REQUIRE((*grb[3]) == 1);
    REQUIRE((*grb[4]) == 2);
  }
}
