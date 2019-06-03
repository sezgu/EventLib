#pragma once
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <initializer_list>
#include <type_traits>

namespace EventLib
{
	struct Subscription
	{
		virtual ~Subscription() = default;
	};

	using EventSubscription = std::unique_ptr<::EventLib::Subscription>;

	template<typename T>
	struct EventResult
	{
		EventResult(T&& ins) :Value{std::forward<T>(ins)} ,m_hasValue(true){}
		EventResult() : Value{GetDefault()}, m_hasValue(false){}
		bool HasValue() { return m_hasValue; }
		using ConstType = typename std::add_const<typename std::remove_reference<T>::type>::type;
		T& operator=(ConstType& value) { Value = value; return Value;}
		EventResult(const EventResult<T>& other):Value(other.Value),m_hasValue(other.m_hasValue){}
	private:
		std::remove_reference_t <T>* m_null_ptr{ nullptr };
		
		template <typename = std::enable_if<!std::is_reference_v<T>>::type>
		T GetDefault() { return T(); }
		template <typename = std::enable_if<std::is_reference_v<T>>::type, typename = void>
		T GetDefault() { return std::forward<T>(*m_null_ptr); }
	public:
		T Value;
	private:
		bool m_hasValue;
	};

	template<>
	struct EventResult<void>
	{
		EventResult(){}
	};
	
	template<typename R, typename ...Args>
	class EventSource
	{
	protected:
		std::shared_ptr<std::recursive_mutex> m_accessLock;
		EventSource(): m_accessLock{ std::make_shared<std::recursive_mutex>() }{}
		virtual EventSubscription AddSubscription(const std::function<R(Args...)>& func) const = 0;
		virtual void RemoveSubscription(EventSubscription& subscription) const = 0;
		virtual ~EventSource() = default;
	public:
		EventSubscription operator+=(const std::function<R(Args...)>& func) const
		{
			std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };
			return AddSubscription(func);
		}

		template <typename T>
		using MethodType = R(T::*)(Args...);

		template <typename T>
		EventSubscription attach(T* instance, MethodType<T> method) const
		{
			std::function<R(Args...)> func = [instance, method](Args&&... args) { return std::invoke(method, *instance, std::forward<Args>(args)...); };
			return operator+=(func);
		}

		void operator-=(EventSubscription& subscription) const
		{
			std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };
			RemoveSubscription(subscription);
		}
	};

	template<typename C,typename R,typename ...Args>
	class EventBase : public EventSource<R,Args...>
	{
	private:
		int GetToken() const
		{
			if (m_subscriptions.empty())
				return 0;
			int max = 0;
			for (auto& sub : m_subscriptions)
			{
				max = std::_Max_value(max, sub.first);
			}
			return max + 1;
		}
	protected:
		struct Subscription final : public ::EventLib::Subscription
		{
			Subscription(const Subscription& other) = delete;
			Subscription& operator=(const Subscription& other) = delete;
			Subscription(Subscription&& other) = default;
			Subscription& operator=(Subscription&& other) = default;

			Subscription(const EventBase& container, int token, const std::function<R(Args...)>& func)
				: m_accessLock(container.m_accessLock)
				, m_detached(false)
				, m_container(&container)
				, m_token(token)
				, m_func(func)
			{}

			R Call(Args&& ... args)
			{
				//No locking here, container will lock to call this.
				return m_func(std::forward<Args>(args)...);
			}

			void Detach()
			{
				//No locking here, container will lock to call this.
				m_detached = true;
			}

			~Subscription() override
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
			std::function<R(Args...)> m_func;
		};
		
		mutable std::map<int, std::reference_wrapper<Subscription>> m_subscriptions{};

		EventSubscription AddSubscription(const std::function<R(Args...)>& func) const override
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
				for (auto& sub : m_subscriptions)
				{
					if (&(sub.second.get()) == ptr)
					{
						m_subscriptions.erase(sub.first);
						ptr->Detach();
						break;
					}
				}
			}
		}

		template <typename = std::enable_if<!std::is_void_v<R>>::type>
		static EventResult<R> Wrap(Subscription& sub,Args&&... args)
		{
			return EventResult<R>{sub.Call(std::forward<Args>(args)...)};
		}

		template <typename = std::enable_if<std::is_void_v<R>>::type,typename = void>
		static EventResult<void> Wrap(Subscription& sub, Args&&... args)
		{
			sub.Call(std::forward<Args>(args)...);
			return EventResult<void>{};
		}
	public:
		EventBase() : EventSource(){}

		virtual ~EventBase()
		{
			std::lock_guard<std::recursive_mutex> lock{*m_accessLock};
			for (auto& sub : m_subscriptions)
			{
				sub.second.get().Detach();
			}
		}

		virtual C Invoke(Args&&... args) = 0;

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
	};

	template<typename R,typename ...Args>
	class Event : public EventBase<EventResult<R>,R,Args...>
	{
	public:
		EventResult<R> Invoke(Args&&... args) override 
		{
			std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };
			auto copiedSubscriptions = m_subscriptions; //Subscriptions are copied because a subscription might be destroyed in the event call.
			for (auto iter = copiedSubscriptions.begin(); iter != copiedSubscriptions.end(); ++iter)
			{
				auto last = --copiedSubscriptions.end();
				if (iter != last)
				{
					iter->second.get().Call(std::forward<Args>(args)...);
				}
				else
				{
					return Wrap(iter->second.get(),std::forward<Args>(args)...);
				}
			}
			return EventResult<R>{};
		}
	};

	template<typename R,typename ...Args>
	class CollectorEvent : public EventBase<std::vector<EventResult<R>>, R, Args...>
	{
	public:
		std::vector<EventResult<R>> Invoke(Args&&... args) override
		{
			std::vector<EventResult<R>> results{};
			std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };
			auto copiedSubscriptions = m_subscriptions; //Subscriptions are copied because a subscription might be destroyed in the event call.
			for (auto iter = copiedSubscriptions.begin(); iter != copiedSubscriptions.end(); ++iter)
			{
				if (!std::is_void<R>::value)
				{
					results.emplace_back(EventResult<R>{iter->second.get().Call(std::forward<Args>(args)...)});
				}
			}
			return results;
		}
	};

	template<typename R, typename ...Args>
	class CombinerEvent : public EventBase<EventResult<R>, R, Args...>
	{
	public:

	private :
		R Combine(const R& first, const R& second) { return m_combiner(const_cast<R&>(first), const_cast<R&>(second));}
		std::function<R(R&, R&)> m_combiner;
	public:
		CombinerEvent(std::function<R(R&, R&)> combiner)
			: m_combiner(combiner)
		{}
		EventResult<R> Invoke(Args&&... args) override
		{
			std::unique_ptr<EventResult<R>> result = std::make_unique<EventResult<R>>();
			std::lock_guard<std::recursive_mutex> lock{ *m_accessLock };

			auto copiedSubscriptions = m_subscriptions; //Subscriptions are copied because a subscription might be destroyed in the event call.
			for (auto iter = copiedSubscriptions.begin(); iter != copiedSubscriptions.end(); ++iter)
			{
				if (!std::is_void<R>::value)
				{
					if (!result->HasValue())
					{
						result = std::make_unique<EventResult<R>>(iter->second.get().Call(std::forward<Args>(args)...));
					}
					else
					{
						result = std::make_unique<EventResult<R>>(Combine(result->Value,iter->second.get().Call(std::forward<Args>(args)...)));
					}
				}
			}
			return *result;
		}
	};

	/*PUT THIS TO THE PRIVATE SECTION OF YOUR TYPE*/
	#define __EVENT(type,access,returnType,eventName,...)													\
	private: 																								\
	##access:																								\
		inline const ::EventLib::EventSource<returnType,__VA_ARGS__>& eventName(){return m_##eventName;}	\
	private:																								\
		::EventLib::Event<returnType,__VA_ARGS__> m_##eventName
	#define EVENT(access,returnType,eventName,...) __EVENT(::EventLib::Event,access,returnType,eventName,__VA_ARGS__){}
	#define COLLECTOREVENT(access,returnType,eventName,...) __EVENT(::EventLib::CollectorEvent,access,returnType,eventName,__VA_ARGS__){}
	#define COMBINEREVENT(access,returnType,eventName,combiner,...) __EVENT(::EventLib::CombinerEvent,access,returnType,eventName,__VA_ARGS__){combiner}
}
