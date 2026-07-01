#include <algorithm>
#include <gtest/gtest.h>
#include <print>

#define private public
#include "MemoryCache.h"
#undef private

template <typename T> void print_regions(const T &container)
{
    for (const auto &el : container)
    {
        std::cout << std::format("{} {}\n", el.range.base, el.range.end());
    }
}

static std::vector<std::pair<uint64_t, uint64_t>> extract(const MemoryCache &c)
{
    std::vector<std::pair<uint64_t, uint64_t>> out;
    for (const auto &r : c.regions())
        out.emplace_back(r.range.base, r.range.end());
    return out;
}

TEST(MemoryCache, RemoveView_SplitMiddle)
{
    MemoryCache c;

    c.add_region({10, 4});
    c.remove_region({11, 2});

    std::set<MemoryCache::Region> expected_result{{10, 1}, {13, 1}};
    EXPECT_EQ(c.regions(), expected_result);
}

TEST(MemoryCache, SingleViewCreatesOneRegion)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType{});
    std::cout << "Regions: \n";
    print_regions(c.regions());
    ASSERT_EQ(c.regions().size(), 1);

    const auto &r = *c.regions().begin();
    EXPECT_EQ(r.range.base, 10);
    EXPECT_EQ(r.range.size, 10);
}

TEST(MemoryCache, OverlappingViewsMergeRegions)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType{});
    c.add_view(15, 10, Interface::ValueType{});

    std::cout << "Regions: \n";
    print_regions(c.regions());
    ASSERT_EQ(c.regions().size(), 1);

    const auto &r = *c.regions().begin();
    EXPECT_EQ(r.range.base, 10);
    EXPECT_EQ(r.range.size, 15);
}

TEST(MemoryCache, TouchingViewsMergeRegions)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType{}); // 10-20
    c.add_view(20, 10, Interface::ValueType{}); // 20-30

    ASSERT_EQ(c.regions().size(), 1);

    const auto &r = *c.regions().begin();
    EXPECT_EQ(r.range.base, 10);
    EXPECT_EQ(r.range.size, 20);
}

TEST(MemoryCache, SeparateViewsStaySeparate)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType{}); // 10-20
    c.add_view(30, 10, Interface::ValueType{}); // 30-40
    std::cout << "Regions: \n";
    print_regions(c.regions());

    std::set<MemoryCache::Region> expected_result{{10, 10}, {30, 10}};
    EXPECT_EQ(c.regions(), expected_result);
}

TEST(MemoryCache, ChainMergeThroughMultipleViews)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType{}); // 10-20
    c.add_view(30, 10, Interface::ValueType{}); // 30-40
    std::cout << "Regions: \n";
    print_regions(c.regions());
    c.add_view(20, 10, Interface::ValueType{}); // 20-30
    std::cout << "Regions: \n";
    print_regions(c.regions());
    ASSERT_EQ(c.regions().size(), 1);

    const auto &r = *c.regions().begin();
    std::set<MemoryCache::Region> expected_result{{10, 30}};
    EXPECT_EQ(c.regions(), expected_result);
    EXPECT_EQ(r.range.base, 10);
    EXPECT_EQ(r.range.size, 30);
}

TEST(MemoryCache, RemoveView_CutLefInterface)
{
    MemoryCache c;

    c.add_region({11, 3});
    c.remove_region({10, 2});

    std::set<MemoryCache::Region> expected_result{{12, 2}};
    EXPECT_EQ(c.regions(), expected_result);
}

TEST(MemoryCache, RemoveView_CutRighInterface)
{
    MemoryCache c;

    c.add_region({10, 3});
    c.remove_region({11, 3});

    std::set<MemoryCache::Region> expected_result{{10, 1}};
    EXPECT_EQ(c.regions(), expected_result);
}

TEST(MemoryCache, RemoveView_MultipleRegions)
{
    MemoryCache c;

    c.add_region({10, 3});
    c.add_region({14, 3});
    c.add_region({18, 3});

    c.remove_region({11, 9});

    std::set<MemoryCache::Region> expected_result{{10, 1}, {20, 1}};
    EXPECT_EQ(c.regions(), expected_result);
}

TEST(MemoryCache, RemoveView_NoOverlap_LefInterface)
{
    MemoryCache c;

    c.add_region({20, 10});
    c.remove_region({10, 5});

    std::set<MemoryCache::Region> expected_result{{20, 10}};
    EXPECT_EQ(c.regions(), expected_result);
}

TEST(MemoryCache, RemoveView_NoOverlap_RighInterface)
{
    MemoryCache c;

    c.add_region({10, 4});
    c.remove_region({20, 10});

    std::set<MemoryCache::Region> expected_result{{10, 4}};
    EXPECT_EQ(c.regions(), expected_result);
}

TEST(MemoryCache, RemoveView_FullErase)
{
    MemoryCache c;

    c.add_region({10, 3});
    c.add_region({14, 3});
    c.add_region({18, 3});

    c.remove_region({0, 30});

    EXPECT_TRUE(c.regions().empty());
}

TEST(MemoryCache, RemoveView_EdgeTouching)
{
    MemoryCache c;

    c.add_region({10, 4});
    c.remove_region({14, 5});

    std::set<MemoryCache::Region> expected_result{{10, 4}};
    EXPECT_EQ(c.regions(), expected_result);
}

TEST(MemoryCache, AddRegionInsideExisting)
{
    MemoryCache c;

    c.add_region({10, 20});
    c.add_region({15, 5});

    std::set<MemoryCache::Region> expected{{10, 20}};
    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddRegionTouchLeft)
{
    MemoryCache c;

    c.add_region({20, 10}); // [20,30)
    c.add_region({10, 10}); // [10,20)

    std::set<MemoryCache::Region> expected{{10, 20}};
    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddRegionTouchRight)
{
    MemoryCache c;

    c.add_region({10, 10}); // [10,20)
    c.add_region({20, 10}); // [20,30)

    std::set<MemoryCache::Region> expected{{10, 20}};
    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddRegionBridgeTwoRegions)
{
    MemoryCache c;

    c.add_region({10, 10}); // [10,20)
    c.add_region({30, 10}); // [30,40)
    c.add_region({20, 10}); // [20,30)

    std::set<MemoryCache::Region> expected{{10, 30}};
    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveMiddleOfMergedRegion)
{
    MemoryCache c;

    c.add_region({10, 30}); // [10,40)

    c.remove_region({20, 10}); // [20,30)

    std::set<MemoryCache::Region> expected{{10, 10}, {30, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveExactRegion)
{
    MemoryCache c;

    c.add_region({10, 10});
    c.remove_region({10, 10});

    EXPECT_TRUE(c.regions().empty());
}

TEST(MemoryCache, RemoveLeftPart)
{
    MemoryCache c;

    c.add_region({10, 10});   // [10,20)
    c.remove_region({10, 5}); // [10,15)

    std::set<MemoryCache::Region> expected{{15, 5}};
    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveRightPart)
{
    MemoryCache c;

    c.add_region({10, 10});   // [10,20)
    c.remove_region({15, 5}); // [15,20)

    std::set<MemoryCache::Region> expected{{10, 5}};
    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveAcrossSeveralRegions)
{
    MemoryCache c;

    c.add_region({10, 5}); // [10,15)
    c.add_region({20, 5}); // [20,25)
    c.add_region({30, 5}); // [30,35)

    c.remove_region({12, 20}); // [12,32)

    std::set<MemoryCache::Region> expected{
        {10, 2}, // [10,12)
        {32, 3}  // [32,35)
    };

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveSingleByteRegion)
{
    MemoryCache c;

    c.add_region({10, 1});
    c.remove_region({10, 1});

    EXPECT_TRUE(c.regions().empty());
}

TEST(MemoryCache, RemoveNonOverlappingGap)
{
    MemoryCache c;

    c.add_region({10, 5}); // [10,15)
    c.add_region({20, 5}); // [20,25)

    c.remove_region({15, 5}); // [15,20)

    std::set<MemoryCache::Region> expected{{10, 5}, {20, 5}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddRegionBetweenTwoSeparateRegions)
{
    MemoryCache c;

    c.add_region({10, 10});  // [10,20)
    c.add_region({100, 10}); // [100,110)

    c.add_region({50, 10}); // [50,60)

    std::set<MemoryCache::Region> expected{{10, 10}, {50, 10}, {100, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddRegionBeforeAllRegions)
{
    MemoryCache c;

    c.add_region({100, 10});
    c.add_region({10, 10});

    std::set<MemoryCache::Region> expected{{10, 10}, {100, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddRegionAfterAllRegions)
{
    MemoryCache c;

    c.add_region({10, 10});
    c.add_region({100, 10});

    std::set<MemoryCache::Region> expected{{10, 10}, {100, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddRegionOverlapOnlyRightNeighbour)
{
    MemoryCache c;

    c.add_region({10, 10}); // [10,20)
    c.add_region({50, 10}); // [50,60)

    c.add_region({45, 10}); // [45,55)

    std::set<MemoryCache::Region> expected{{10, 10}, {45, 15}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddRegionOverlapOnlyLeftNeighbour)
{
    MemoryCache c;

    c.add_region({10, 10}); // [10,20)
    c.add_region({50, 10}); // [50,60)

    c.add_region({15, 10}); // [15,25)

    std::set<MemoryCache::Region> expected{{10, 15}, {50, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveRegionFromGap)
{
    MemoryCache c;

    c.add_region({10, 10}); // [10,20)
    c.add_region({50, 10}); // [50,60)

    c.remove_region({25, 10}); // [25,35)

    std::set<MemoryCache::Region> expected{{10, 10}, {50, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveRegionTouchingGapLeftEdge)
{
    MemoryCache c;

    c.add_region({10, 10}); // [10,20)
    c.add_region({30, 10}); // [30,40)

    c.remove_region({20, 10}); // [20,30)

    std::set<MemoryCache::Region> expected{{10, 10}, {30, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, AddHugeCoveringRegion)
{
    MemoryCache c;

    c.add_region({10, 10});
    c.add_region({30, 10});
    c.add_region({50, 10});

    c.add_region({0, 100});

    std::set<MemoryCache::Region> expected{{0, 100}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveRegionLeavingTwoOuterFragments)
{
    MemoryCache c;

    c.add_region({10, 5}); // [10,15)
    c.add_region({20, 5}); // [20,25)
    c.add_region({30, 5}); // [30,35)

    c.remove_region({12, 21}); // [12,33)

    std::set<MemoryCache::Region> expected{
        {10, 2}, // [10,12)
        {33, 2}  // [33,35)
    };

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveOneOfOverlappingViewsKeepsSharedPart)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType_Int32); // [10,20)
    c.add_view(15, 10, Interface::ValueType_Int32); // [15,25)

    c.remove_view(10, 10, Interface::ValueType_Int32);

    std::set<MemoryCache::Region> expected{{15, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveLastViewRemovesRegion)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType_Int32);

    c.remove_view(10, 10, Interface::ValueType_Int32);

    EXPECT_TRUE(c.views().empty());
    EXPECT_TRUE(c.regions().empty());
}

TEST(MemoryCache, SameRangeDifferentTypes)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType_Int32);
    c.add_view(10, 10, Interface::ValueType_Float);

    ASSERT_EQ(c.views().size(), 2);
    ASSERT_EQ(c.regions().size(), 1);

    c.remove_view(10, 10, Interface::ValueType_Int32);

    ASSERT_EQ(c.views().size(), 1);

    std::set<MemoryCache::Region> expected{{10, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveMiddleViewLeavesTwoFragments)
{
    MemoryCache c;

    c.add_view(10, 30, Interface::ValueType_Int32); // [10,40)
    c.add_view(10, 10, Interface::ValueType_Int32); // [10,20)
    c.add_view(30, 10, Interface::ValueType_Int32); // [30,40)

    c.remove_view(10, 30, Interface::ValueType_Int32);

    std::set<MemoryCache::Region> expected{{10, 10}, {30, 10}};

    EXPECT_EQ(c.regions(), expected);
}

TEST(MemoryCache, RemoveNonExistingViewDoesNothing)
{
    MemoryCache c;

    c.add_view(10, 10, Interface::ValueType_Int32);

    auto before_regions = c.regions();
    auto before_views = c.views();

    c.remove_view(20, 10, Interface::ValueType_Int32);

    EXPECT_EQ(c.regions(), before_regions);
    EXPECT_EQ(c.views(), before_views);
}

TEST(MemoryCache, AddZeroSizeViewDoesNothing)
{
    MemoryCache c;

    c.add_view(10, 0, Interface::ValueType_Int32);

    EXPECT_TRUE(c.views().empty());
    EXPECT_TRUE(c.regions().empty());
}

TEST(MemoryCache, PutStoresDataInRegion)
{
    MemoryCache c;

    c.put({1, 2, 3, 4}, MemoryCache::View{{10, 4}, Interface::ValueType_ByteArray});

    ASSERT_EQ(c.regions().size(), 1);

    const auto &r = *c.regions().begin();
    EXPECT_EQ(r.range.base, 10);
    EXPECT_EQ(r.range.size, 4);
    EXPECT_EQ(r.data, (std::vector<uint8_t>{1, 2, 3, 4}));
}

TEST(MemoryCache, PutMergesAndPlacesDataByAddress)
{
    MemoryCache c;

    c.put({1, 2}, MemoryCache::View{{10, 2}, Interface::ValueType_ByteArray});
    c.put({5, 6}, MemoryCache::View{{14, 2}, Interface::ValueType_ByteArray});
    c.put({3, 4}, MemoryCache::View{{12, 2}, Interface::ValueType_ByteArray});

    ASSERT_EQ(c.regions().size(), 1);

    const auto &r = *c.regions().begin();
    EXPECT_EQ(r.range.base, 10);
    EXPECT_EQ(r.range.size, 6);
    EXPECT_EQ(r.data, (std::vector<uint8_t>{1, 2, 3, 4, 5, 6}));
}

TEST(MemoryCache, PutOverwritesOverlappingBytes)
{
    MemoryCache c;

    c.put({1, 2, 3, 4}, MemoryCache::View{{10, 4}, Interface::ValueType_ByteArray});
    c.put({8, 9}, MemoryCache::View{{12, 2}, Interface::ValueType_ByteArray});

    ASSERT_EQ(c.regions().size(), 1);

    const auto &r = *c.regions().begin();
    EXPECT_EQ(r.range.base, 10);
    EXPECT_EQ(r.range.size, 4);
    EXPECT_EQ(r.data, (std::vector<uint8_t>{1, 2, 8, 9}));
}
