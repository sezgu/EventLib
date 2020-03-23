#include "pch.h"
#include "Event.h"
#include <string>

using namespace EventLib;

TEST(EventTests, SubUnsubLambda)
{
	Event<void,bool&> myEvent{};

	auto sub = myEvent += [](bool& value)
	{
		value = true;
	};

	bool called = false;
	myEvent.Invoke(called);

	ASSERT_TRUE(called);
	called = false;
	myEvent -= sub;

	myEvent.Invoke(called);

	ASSERT_FALSE(called);
}

namespace SubUnsubMethodRecursive
{
	class EventClient
	{
	private:
		EventSubscription sub;
		std::function<void()> m_trigger;
		void Method(bool& value, int& counter)
		{
			++counter;
			if (!value)
			{
				value = true;
				m_trigger();
			}
		}
	public:
		EventClient(const EventSource<void, bool&, int&>& event,std::function<void()> trigger)
			:sub(event.attach(this, &EventClient::Method))
			, m_trigger(trigger)
		{
		}
	};
}

TEST(EventTests, SubUnsubMethodRecursive)
{
	Event<void, bool&,int&> myEvent{};
	bool called = false;
	int counter = 0;
	SubUnsubMethodRecursive::EventClient client{ myEvent,
		[&myEvent, &called, &counter]() 
			{myEvent.Invoke(called,counter); } 
	};

	myEvent.Invoke(called,counter);

	ASSERT_EQ(2, counter);
	ASSERT_EQ(true, called);

	myEvent.Invoke(called, counter);
	
	ASSERT_EQ(3, counter);
	ASSERT_EQ(true, called);
}

TEST(EventTests, UnsubInEventCall)
{
	Event<int> myEvent{};

	EventSubscription sub;
	sub = myEvent+= [&myEvent,&sub]() -> int
	{
		myEvent -= sub;
		return 5;
	};

	auto result = myEvent.Invoke();
	ASSERT_TRUE(result.HasValue());
	ASSERT_EQ(5, result.Value);

	auto secondResult = myEvent.Invoke();

	ASSERT_FALSE(secondResult.HasValue());

}

TEST(EventTests, ResultTypes)
{
	{
		EventResult<bool> valueResultDefault{};
		ASSERT_FALSE(valueResultDefault.HasValue());
		ASSERT_FALSE(valueResultDefault.Value);
	}
	{
		EventResult<bool> valueResultValued{ true };
		ASSERT_TRUE(valueResultValued.HasValue());
		ASSERT_TRUE(valueResultValued.Value);
		valueResultValued = false;
		ASSERT_FALSE(valueResultValued.Value);
	}
	{
		EventResult<int&> refResultDefault{};
		ASSERT_FALSE(refResultDefault.HasValue());
		ASSERT_EQ(nullptr, &refResultDefault.Value);
	}
	{
		int intValue = 5;
		EventResult<int&> refResultValued{ intValue };
		ASSERT_TRUE(refResultValued.HasValue());
		ASSERT_EQ(5, refResultValued.Value);
		refResultValued.Value = 4;
		ASSERT_EQ(4, intValue);
		refResultValued = 7;
		ASSERT_EQ(7, intValue);
	}
	{
		EventResult<int&&> rrefResultDefault{};
		ASSERT_FALSE(rrefResultDefault.HasValue());
		ASSERT_EQ(nullptr, &rrefResultDefault.Value);
	}
	{
		EventResult<int&&> rrefResultValued{ 2 };
		ASSERT_TRUE(rrefResultValued.HasValue());
		ASSERT_EQ(2, rrefResultValued.Value);
	}

}

TEST(EventTests, InvokeValueReturnNoSub)
{
	Event<int> myEvent{};

	auto result = myEvent.Invoke();
	ASSERT_FALSE(result.HasValue());
	ASSERT_EQ(0, result.Value);
}

TEST(EventTests, InvokeValueReturnMultiSubs)
{
	Event<int, int&> myEvent{};
	std::vector<EventSubscription> subs;
	for (int i = 0; i < 10; ++i)
	{
		subs.push_back(myEvent += [](int& val) -> int 
		{
			return val++;
		});
	}
	int counter = 0;
	auto result = myEvent.Invoke(counter);
	ASSERT_TRUE(result.HasValue());
	ASSERT_EQ(9, result.Value);
	ASSERT_EQ(10, counter);

}

TEST(EventTests, InvokeRefReturnNoSub)
{
	Event<int&> myEvent{};

	auto result = myEvent.Invoke();
	ASSERT_FALSE(result.HasValue());
	ASSERT_EQ(nullptr, &result.Value);
}

TEST(EventTests, InvokeRefReturnMultiSubs)
{
	Event<int&> myEvent{};
	std::vector<EventSubscription> subs;
	std::vector<int> values(10);
	for (int i = 0; i < 10; ++i)
	{
		subs.emplace_back(myEvent += [&values,i]() -> int&
		{
			return values.at(i);
		});
	}

	auto result = myEvent.Invoke();
	ASSERT_TRUE(result.HasValue());
	ASSERT_EQ(0, values.at(9));
	result.Value = 3;
	ASSERT_EQ(3, values.at(9));
	for (int i = 0; i < 9; ++i)
	{
		ASSERT_EQ(0, values.at(i));
	}

}


TEST(EventTests, MoveOnlyArg)
{
	Event<void,std::unique_ptr<int>> myEvent{};
	
	std::unique_ptr<int> value;
	auto sub = myEvent += [&](std::unique_ptr<int> val)
	{
		value = std::move(val);
	};

	myEvent.Invoke(std::make_unique<int>(5));

	ASSERT_TRUE(value != nullptr);
	ASSERT_EQ(5, *value);
}

namespace ClassEvent
{
	class ClassWithEvent
	{
		int m_value{ 0 };
		EVENT(public, void, SomeEvent, int&);
	public:
		void DoSomeStuff()
		{
			m_value = 0;
			m_SomeEvent.Invoke(m_value);
		}
		int GetValue()
		{
			return m_value;
		}
	};
}

TEST(EventTests, ClassEvent)
{
	ClassEvent::ClassWithEvent instance{};

	auto sub = instance.SomeEvent() += [](int& val) {
		val = 10;
	};

	ASSERT_EQ(0, instance.GetValue());
	instance.DoSomeStuff();
	ASSERT_EQ(10, instance.GetValue());

	sub.reset();
	instance.DoSomeStuff();
	ASSERT_EQ(0, instance.GetValue());

}

TEST(EventTests, NonCopyableResult)
{
	Event<std::unique_ptr<int>, int> myEvent{};

	auto sub = myEvent += [](int value) {

		return std::make_unique<int>(value);
	};

	auto result = myEvent.Invoke(7);

	ASSERT_TRUE(result.HasValue());
	ASSERT_EQ(7, *result.Value);

	sub.reset();

	auto otherResult = myEvent.Invoke(5);
	ASSERT_FALSE(otherResult.HasValue());
	ASSERT_EQ(nullptr, otherResult.Value);

}

