#include "pch.h"
#include "Event.h"

using namespace EventLib;

TEST(CollectorEventTests, InvokeWithoutSub)
{
	CollectorEvent<int> myEvent{};

	auto results = myEvent.Invoke();

	ASSERT_EQ(0, results.size());

	
		
}

TEST(CollectorEventTests, InvokeValueReturnMultiSub)
{
	CollectorEvent<int> myEvent{};
	std::vector<EventSubscription> subs;
	for (int i = 0; i < 10; ++i)
	{
		subs.emplace_back(myEvent += [i]() -> int
		{
			return i;
		});
	}

	auto results = myEvent.Invoke();
	for (int i = 0; i < 10; ++i)
	{
		ASSERT_TRUE(results.at(i).HasValue());
		ASSERT_EQ(i, results.at(i).Value);
	}
	

}

TEST(CollectorEventTests, InvokeRefReturnMultiSub)
{
	CollectorEvent<int&> myEvent{};
	std::vector<EventSubscription> subs;
	std::vector<int> values(10);
	
	for (int i = 9; i >= 0; --i)
	{
		subs.emplace_back(myEvent += [&values, i]() -> int&
		{
			return values.at(i); 
		});
	}
	
	auto result = myEvent.Invoke();
	ASSERT_EQ(10, result.size());
	for (int i = 0; i < 10; ++i)
	{
		result.at(i) = i + 1;
	}
	
	for (int i = 9; i >= 0; --i)
	{
		ASSERT_EQ(i + 1, values.at(9 - i));
	}

}
