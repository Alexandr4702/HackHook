#include <gtest/gtest.h>
#include "MemoryCache.h"


#include <print>

template<typename T>
void print_regions(const T& container)
{
    for(const auto& el : container)
    {
        std::cout << std::format("{} {}\n",
                                 el.range.base,
                                 el.range.size);
    }
}

TEST(MemoryCache, SingleViewCreatesOneRegion)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType{});
    std::cout << "Regions: \n";
    print_regions(c.regions());
    ASSERT_EQ(c.regions().size(), 1);
    

    const auto& r = *c.regions().begin();
    EXPECT_EQ(r.range.base, 10);
    EXPECT_EQ(r.range.size, 10);
}

// TEST(MemoryCache, OverlappingViewsMergeRegions)
// {
//     MemoryCache c;

//     c.add_view(10, 10, Interface::ValueType{});
//     c.add_view(15, 10, Interface::ValueType{});

//     ASSERT_EQ(c.regions().size(), 1);

//     const auto& r = *c.regions().begin();
//     EXPECT_EQ(r.range.base, 10);
//     EXPECT_EQ(r.range.size, 15);
// }

// TEST(MemoryCache, TouchingViewsMergeRegions)
// {
//     MemoryCache c;

//     c.add_view(10, 10, Interface::ValueType{}); // 10-20
//     c.add_view(20, 10, Interface::ValueType{}); // 20-30

//     ASSERT_EQ(c.regions().size(), 1);

//     const auto& r = *c.regions().begin();
//     EXPECT_EQ(r.range.base, 10);
//     EXPECT_EQ(r.range.size, 20);
// }

// TEST(MemoryCache, SeparateViewsStaySeparate)
// {
//     MemoryCache c;

//     c.add_view(10, 10, Interface::ValueType{}); // 10-20
//     c.add_view(30, 10, Interface::ValueType{}); // 30-40

//     ASSERT_EQ(c.regions().size(), 2);
// }

// TEST(MemoryCache, ChainMergeThroughMultipleViews)
// {
//     MemoryCache c;

//     c.add_view(10, 10, Interface::ValueType{}); // 10-20
//     c.add_view(20, 10, Interface::ValueType{}); // 20-30
//     c.add_view(30, 10, Interface::ValueType{}); // 30-40

//     ASSERT_EQ(c.regions().size(), 1);

//     const auto& r = *c.regions().begin();
//     EXPECT_EQ(r.range.base, 10);
//     EXPECT_EQ(r.range.size, 30);
// }

// TEST(MemoryCache, ViewRemovalDoesNotBreakRegionIfStillCovered)
// {
//     MemoryCache c;

//     c.add_view(10, 20, Interface::ValueType{});
//     c.add_view(15, 5, Interface::ValueType{});

//     // region still should exist
//     ASSERT_EQ(c.regions().size(), 1);

//     c.remove_view(15, 5, Interface::ValueType{});

//     // still covered by first view
//     ASSERT_EQ(c.regions().size(), 1);
// }

// TEST(MemoryCache, ViewRemovalCanSplitRegion)
// {
//     MemoryCache c;

//     c.add_view(10, 10, Interface::ValueType{});
//     c.add_view(20, 10, Interface::ValueType{});

//     ASSERT_EQ(c.regions().size(), 1);

//     c.remove_view(10, 10, Interface::ValueType{});
//     c.remove_view(20, 10, Interface::ValueType{});

//     // depending on your remove_region implementation
//     // expected: no regions left
//     ASSERT_EQ(c.regions().size(), 0);
// }