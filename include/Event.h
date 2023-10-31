#ifndef EVENT_H
#define EVENT_H
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <initializer_list>
#include <type_traits>
#include <algorithm>

namespace EventLib
{
	struct Subscription
	{
		virtual ~Subscription() noexcept = default;
	};

	using EventSubscription = std::unique_ptr<::EventLib::Subscription>;

	template<typename T>
	struct EventResult
	{
		template<class Q = T>
		typename std::enable_if<std::is_reference<Q>::value && !std::is_rvalue_reference<Q>::value, T>::type GetDefault()
		{
			return *m_null_ptr;
		}

		template<class Q = T>
		typename std::enable_if<!std::is_reference<Q>::value, T>::type GetDefault()
		{
			return T();
		}

		template<class Q = T>
		typename std::enable_if<std::is_rvalue_reference<Q>::value, T>::type GetDefault()
		{
			return std::move(*m_null_ptr);
		}

		EventResult(T&& ins) :Value{ std::forward<T>(ins) }, m_hasValue(true) {}
		EventResult() : Value{ GetDefault<T>() }, m_hasValue(false) {}
		bool HasValue() { return m_hasValue; }
		using ConstType = typename std::add_const<typename std::remove_reference<T>::type>::type;
		T& operator=(ConstType& value) { Value = value; return Value; }
		EventResult(const EventResult<T>& other) :Value(other.Value), m_hasValue(other.m_hasValue) {}

		std::remove_reference_t <T>* m_null_ptr{ nullptr };
	public:
		T Value;
	private:
		bool m_hasValue;
	};

	template<>
	struct EventResult<void>
	{
		EventResult() {}
	};

	template<typename TReturn, typename ...Args>
	class EventSource
	{
	protected:
		std::shared_ptr<std::recursive_mutex> m_accessLock;
		EventSource() : m_accessLock{ std::make_shared<std::recursive_mutex>() } {}
		virtual EventSubscription AddSubscription(const std::function<TReturn(Args...)>& func) const = 0;
		virtual void RemoveSubscription(EventSubscription& subscription) const = 0;
		virtual ~EventSource() = default;

		std::recursive_mutex& get_access_lock()
		{
			return *m_accessLock;
		}

	public:
		EventSubscription operator+=(const std::function<TReturn(Args...)>& func) const
		{
			std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };
			return AddSubscription(func);
		}

		void operator-=(EventSubscription& subscription) const
		{
			std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };
			RemoveSubscription(subscription);
		}

		//template <typename T, typename ...TArgs>
		//using MethodType = TReturn(T::*)(TArgs...);
		//
		//template <typename T>
		//EventSubscription attach(T* instance, MethodType<T, Args...> method) const
		//{
		//	std::function<TReturn(Args...)> func = [instance, method](Args&&... args) { return std::invoke(method, *instance, std::forward<Args>(args)...); };
		//	return operator+=(func);
		//}


	};

	template<typename TResult, typename TReturn, typename ...Args>
	class EventBase : public EventSource<TReturn, Args...>
	{
		using EventSource<TReturn, Args...>::m_accessLock;

		int GetToken() const
		{
			if (m_subscriptions.empty())
				return 0;
			int max = 0;
			for (auto& sub : m_subscriptions)
			{
				max = std::max(max, sub.first);
			}
			return max + 1;
		}
		void Remove(Subscription* subscription) const
		{
			//No locking here, subscription will lock to call this.
			for (auto& sub : m_subscriptions)
			{
				if (&(sub.second.get()) == subscription)
				{
					m_subscriptions.erase(sub.first);
					break;
				}
			}
		}

	protected:
		struct Subscription final : public ::EventLib::Subscription
		{
			Subscription(const Subscription& other) = delete;
			Subscription& operator=(const Subscription& other) = delete;
			Subscription(Subscription&& other) = default;
			Subscription& operator=(Subscription&& other) = default;

			Subscription(const EventBase& container, int token, const std::function<TReturn(Args...)>& func)
				: m_accessLock(container.m_accessLock)
				, m_detached(false)
				, m_container(&container)
				, m_token(token)
				, m_func(func)
			{}

			TReturn Call(Args&& ... args)
			{
				//No locking here, container will lock to call this.
				return m_func(std::forward<Args>(args)...);
			}

			void Detach()
			{
				//No locking here, container will lock to call this.
				m_detached = true;
			}

			~Subscription() noexcept override
			{
				std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };
				if (!m_detached)
				{
					m_container->Remove(this);
				}
			}
		private:
			std::shared_ptr<std::recursive_mutex> m_accessLock;
			bool m_detached;
			const EventBase* m_container;
			int m_token;
			std::function<TReturn(Args...)> m_func;
		};

		mutable std::map<int, std::reference_wrapper<Subscription>> m_subscriptions{};

		EventSubscription AddSubscription(const std::function<TReturn(Args...)>& func) const override
		{
			auto token = GetToken();
			auto retval = std::make_unique<Subscription>(*this, token, func);

			m_subscriptions.emplace(token, *retval);

			return std::move(retval);
		}
		void RemoveSubscription(EventSubscription& subscription) const override
		{
			Subscription* ptr = dynamic_cast<Subscription*>(subscription.get());
			if (ptr)
			{
				Remove(ptr);
			}
		}

		template<class Q = TReturn>
		typename std::enable_if<!std::is_void<Q>::value, EventResult<Q>>::type Wrap(Subscription& sub, Args&&... args)
		{
			return EventResult<Q>{sub.Call(std::forward<Args>(args)...)};
		}

		template<class Q = TReturn>
		typename std::enable_if<std::is_void<Q>::value, EventResult<void>>::type Wrap(Subscription& sub, Args&&... args)
		{
			sub.Call(std::forward<Args>(args)...);
			return EventResult<void>{};
		}
	public:
		EventBase() : EventLib::EventSource<TReturn, Args ...>() {}

		virtual ~EventBase()
		{
			std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };
			for (auto& sub : m_subscriptions)
			{
				sub.second.get().Detach();
			}
		}

		virtual TResult Invoke(Args ... args) = 0;


	};

	template<typename TReturn, typename ...Args>
	class Event : public EventBase<EventResult<TReturn>, TReturn, Args...>
	{
	public:
		EventResult<TReturn> Invoke(Args... args) override
		{
			std::lock_guard<std::recursive_mutex> lock{ this->get_access_lock() };
			auto copiedSubscriptions = this->m_subscriptions; //Subscriptions are copied because a subscription might be destroyed in the event call.
			for (auto iter = copiedSubscriptions.begin(); iter != copiedSubscriptions.end(); ++iter)
			{
				auto last = --copiedSubscriptions.end();
				if (iter != last)
				{
					iter->second.get().Call(std::forward<Args>(args)...);
				}
				else
				{
					return this->Wrap(iter->second.get(), std::forward<Args>(args)...);
				}
			}
			return EventResult<TReturn>{};
		}
	};

	template<typename TReturn, typename ...Args>
	class CollectorEvent : public EventBase<std::vector<EventResult<TReturn>>, TReturn, Args...>
	{
		//using EventSource<TReturn, Args...>::m_accessLock;
		//using EventSource<TReturn, Args...>::m_subscriptions;
	public:
		std::vector<EventResult<TReturn>> Invoke(Args... args) override
		{
			std::vector<EventResult<TReturn>> results{};
			std::lock_guard<std::recursive_mutex> lock{ this->get_access_lock() };
			auto copiedSubscriptions = m_subscriptions; //Subscriptions are copied because a subscription might be destroyed in the event call.
			for (auto iter = copiedSubscriptions.begin(); iter != copiedSubscriptions.end(); ++iter)
			{
				if (!std::is_void<TReturn>::value)
				{
					results.emplace_back(EventResult<TReturn>{iter->second.get().Call(std::forward<Args>(args)...)});
				}
			}
			return results;
		}
	};

	template<typename TReturn, typename ...Args>
	class CombinerEvent : public EventBase<EventResult<TReturn>, TReturn, Args...>
	{
		//using EventSource<TReturn, Args...>::m_accessLock;
		//using EventSource<TReturn, Args...>::m_subscriptions;

		TReturn Combine(const TReturn& first, const TReturn& second) { return m_combiner(const_cast<TReturn&>(first), const_cast<TReturn&>(second)); }
		std::function<TReturn(TReturn&, TReturn&)> m_combiner;
	public:
		CombinerEvent(std::function<TReturn(TReturn&, TReturn&)> combiner)
			: m_combiner(combiner)
		{}
		EventResult<TReturn> Invoke(Args... args) override
		{
			std::unique_ptr<EventResult<TReturn>> result = std::make_unique<EventResult<TReturn>>();
			std::lock_guard<std::recursive_mutex> lock{ this->get_access_lock() };

			auto copiedSubscriptions = m_subscriptions; //Subscriptions are copied because a subscription might be destroyed in the event call.
			for (auto iter = copiedSubscriptions.begin(); iter != copiedSubscriptions.end(); ++iter)
			{
				if (!std::is_void<TReturn>::value)
				{
					if (!result->HasValue())
					{
						result = std::make_unique<EventResult<TReturn>>(iter->second.get().Call(std::forward<Args>(args)...));
					}
					else
					{
						result = std::make_unique<EventResult<TReturn>>(Combine(result->Value, iter->second.get().Call(std::forward<Args>(args)...)));
					}
				}
			}
			return *result;
		}
	};

}

#endif

#ifdef ENABLE_EVENT_MACROS
#ifndef __EVENT

#define __EVENT(type,access,returnType,eventName,...) 														\
access:																										\
	inline const ::EventLib::EventSource<returnType,__VA_ARGS__>& eventName() const{return m_##eventName;}	\
private:																								    \
	type<returnType,__VA_ARGS__> m_##eventName															



/*PUT THIS TO THE PRIVATE SECTION OF YOUR TYPE*/
#define EVENT_ITF(access,returnType,eventName,...)														\
private:                                                                                                \
##access:                                                                                               \
	virtual const ::EventLib::EventSource<returnType, __VA_ARGS__>& eventName() const = 0                     

	/*PUT THIS TO THE END OF PRIVATE SECTION OF YOUR TYPE*/
#define EVENT(access,returnType,eventName,...) __EVENT(::EventLib::Event,access,returnType,eventName,__VA_ARGS__){}
/*PUT THIS TO THE END OF PRIVATE SECTION OF YOUR TYPE*/
#define COLLECTOREVENT(access,returnType,eventName,...) __EVENT(::EventLib::CollectorEvent,access,returnType,eventName,__VA_ARGS__){}
/*PUT THIS TO THE END OF PRIVATE SECTION OF YOUR TYPE*/
#define COMBINEREVENT(access,returnType,eventName,combiner,...) __EVENT(::EventLib::CombinerEvent,access,returnType,eventName,__VA_ARGS__){combiner}

#endif
#endif // !ENABLE_EVENT_MACROS