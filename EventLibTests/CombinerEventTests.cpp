#include "pch.h"
#include "Event.h"

using namespace EventLib;

TEST(CombinerEventTests, InvokeNoSub)
{
	CombinerEvent<int> myEvent{ [](const int&,const int&)->int 
	{
		return 0;
	}};

	auto result = myEvent.Invoke();
	ASSERT_FALSE(result.HasValue());


}

TEST(CombinerEventTests, InvokeSingleSub)
{
	CombinerEvent<int> myEvent{ [](const int&,const int&)->int
	{
		return -1;
	} };

	auto sub = myEvent += []() -> int
	{
		return 5;
	};

	auto result = myEvent.Invoke();
	ASSERT_TRUE(result.HasValue());
	ASSERT_EQ(5, result.Value);

}

TEST(CombinerEventTests, InvokeValueReturnMultiSub)
{
	CombinerEvent<int> myEvent{ [](int& first,int& second)->int
	{
		return first >= second ? first : second;
	}};

	std::vector<int> values(size_t{100});
	std::vector<EventSubscription> subs;
	for (auto& value : values)
	{
		value = std::abs(std::rand());
		subs.emplace_back(myEvent += [value]() -> int
		{
			return value;
		});
	}

	auto result = myEvent.Invoke();

	int maxValue = values.at(0);
	for (auto& value : values)
	{
		if (value > maxValue) 
		{
			maxValue = value;
		}
	}
	
	ASSERT_TRUE(result.HasValue());
	ASSERT_EQ(maxValue, result.Value);
	   
}

struct NonCopyableInt
{
	int Val;
	NonCopyableInt() :Val(0) {}
	NonCopyableInt(int val):Val(val){}
	NonCopyableInt(const NonCopyableInt& other) = delete;
	NonCopyableInt& operator=(const NonCopyableInt& other) = delete;
};

TEST(CombinerEventTests, InvokeRefReturnMultiSub)
{
	CombinerEvent<NonCopyableInt&> myEvent{ [](NonCopyableInt& first,NonCopyableInt& second)->NonCopyableInt&
	{
		return first.Val >= second.Val ? first : second;
	} };

	std::vector<NonCopyableInt> values(size_t{ 100 });
	std::vector<EventSubscription> subs;
	for (auto& value : values)
	{
		value.Val = std::abs(std::rand());
		subs.emplace_back(myEvent += [&value]() -> NonCopyableInt&
		{
			return value;
		});
	}

	auto result = myEvent.Invoke();

	auto* maxValue = &values.at(0);
	for (auto& value : values)
	{
		if (value.Val > maxValue->Val)
		{
			maxValue = &value;
		}
	}

	auto resultPtr = &result.Value;
	ASSERT_EQ(maxValue, resultPtr);
	ASSERT_TRUE(result.HasValue());
	ASSERT_EQ(maxValue->Val, result.Value.Val);

	maxValue->Val = -1;
	ASSERT_EQ(-1, result.Value.Val);


}
