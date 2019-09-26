/*

The MIT License (MIT)

Copyright (c) 2015 Ievgen Polyvanyi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Original code https://github.com/cpp11nullptr Ievgen Polyvanyi
Cloned to https://github.com/balmerdx/lsignal
*/

#pragma once

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace lsignal
{
	template<int>
	struct placeholder_lsignal
	{
	};
}

namespace std
{
	// custom std::placeholders

	template<int N>
	struct is_placeholder<lsignal::placeholder_lsignal<N>>
		: integral_constant<int, N+1>
	{
	};
}

namespace lsignal
{
	// std::integer_sequence for C++11

	template<int... Ns>
	struct int_sequence
	{
	};

	template<int N, int... Ns>
	struct make_int_sequence
		: make_int_sequence<N-1, N-1, Ns...>
	{
	};

	template<int... Ns>
	struct make_int_sequence<0, Ns...>
		: int_sequence<Ns...>
	{
	};

	// connection

	struct connection_data
	{
		bool locked;
	};

	struct connection_cleaner
	{
		std::function<void(std::shared_ptr<connection_data>)> deleter;
		std::shared_ptr<connection_data> data;

		connection_cleaner();
		~connection_cleaner();
	};

	class connection
	{
		template<typename>
		friend class signal;

	public:
		connection();
		connection(std::shared_ptr<connection_data>&& data);
		virtual ~connection();

		bool is_locked() const;
		void set_lock(const bool lock);

		void disconnect();

	private:
		std::shared_ptr<connection_data> _data;
		std::vector<connection_cleaner> _cleaners;

	};


	// slot
	class slot
		: public connection
	{
	public:
		slot();
		~slot() override;

	};

	// signal
	template<typename>
	class signal;

	template<typename R, typename... Args>
	class signal<R(Args...)>
	{
	public:
		using result_type = R;
		using callback_type = std::function<R(Args...)>;

		signal();
		~signal();

		signal(const signal& rhs);
		signal& operator= (const signal& rhs);

		signal(signal&& rhs) = default;
		signal& operator= (signal&& rhs) = default;

		bool is_locked() const;
		void set_lock(const bool lock);

		//Child signals not tested!!!! And shuld be not worked propertly!!!!!
		void connect(signal *sg);
		void disconnect(signal *sg);

		connection connect(const callback_type& fn, slot *owner);
		connection connect(callback_type&& fn, slot *owner);

		template<typename T, typename U>
		connection connect(T *p, const U& fn, slot *owner);

		void disconnect(const connection& connection);
		void disconnect(slot *owner);

		void disconnect_all();

		//Return last called signal result.
		R operator() (Args... args) const;

		//!!! Not tested!!!!! And shuld be not worked propertly!!!!!
		template<typename T>
		R operator() (Args... args, const T& agg) const;

		//this signal dont have direct connections
		bool empty() const;
	private:
		struct joint
		{
			callback_type callback;
			std::shared_ptr<connection_data> connection;
			slot *owner;
		};

		struct internal_data
		{
			mutable std::mutex _mutex;
			bool _locked = false;
			bool _signal_called = false;

			std::list<joint> _callbacks;

			signal *_parent = nullptr;
			std::list<signal*> _children;

			std::list<joint> _deffered_add_connection;
			std::list<std::shared_ptr<connection_data> > _deffered_delete_connection;
		};

		std::shared_ptr<internal_data> _data;


		template<typename T, typename U, int... Ns>
		callback_type construct_mem_fn(const T& fn, U *p, int_sequence<Ns...>) const;

		void copy_callbacks(const std::list<joint>& callbacks);

		std::shared_ptr<connection_data> create_connection(callback_type&& fn, slot *owner);
		void destroy_connection(std::shared_ptr<connection_data> connection);
		void destroy_connection_internal(std::shared_ptr<connection_data>& connection) const;
		void add_and_delete_deffered() const;

		connection prepare_connection(connection&& conn);

	};

	template<typename R, typename... Args>
	signal<R(Args...)>::signal()
		: _data(std::make_shared<internal_data>())
	{
	}

	template<typename R, typename... Args>
	signal<R(Args...)>::~signal()
	{
		internal_data* data = _data.get();
		std::lock_guard<std::mutex> locker(data->_mutex);

		for (const joint& jnt : data->_callbacks)
		{
			if (jnt.owner != nullptr)
			{
				jnt.owner->_data = nullptr;
				jnt.owner->_cleaners.clear();
			}
		}

		if (data->_parent != nullptr)
		{
			data->_parent->_data->_children.remove(this);
		}

		for (signal *sig : data->_children)
		{
			sig->_data->_parent = nullptr;
		}
	}

	template<typename R, typename... Args>
	signal<R(Args...)>::signal(const signal& rhs)
		: _data(std::make_shared<internal_data>())
	{
		internal_data* data = _data.get();
		internal_data* rhs_data = rhs._data.get();

		std::unique_lock<std::mutex> lock_own(data->_mutex, std::defer_lock);
		std::unique_lock<std::mutex> lock_rhs(rhs_data->_mutex, std::defer_lock);

		std::lock(lock_own, lock_rhs);

		data->_locked = rhs_data->_locked;

		copy_callbacks(rhs_data->_callbacks);
	}

	template<typename R, typename... Args>
	signal<R(Args...)>& signal<R(Args...)>::operator= (const signal& rhs)
	{
		internal_data* data = _data.get();
		internal_data* rhs_data = rhs._data.get();

		std::unique_lock<std::mutex> lock_own(data->_mutex, std::defer_lock);
		std::unique_lock<std::mutex> lock_rhs(rhs_data->_mutex, std::defer_lock);

		std::lock(lock_own, lock_rhs);

		data->_locked = rhs_data->_locked;

		copy_callbacks(rhs_data->_callbacks);

		return *this;
	}

	template<typename R, typename... Args>
	bool signal<R(Args...)>::is_locked() const
	{
		return _data.get()->_locked;
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::set_lock(const bool lock)
	{
		_data.get()->_locked = lock;
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::connect(signal *sg)
	{
		internal_data* data = _data.get();
		std::lock_guard<std::mutex> locker_own(data->_mutex);
		std::lock_guard<std::mutex> locker_sg(sg->_data->_mutex);

		if (data->_parent == sg)
		{
			return;
		}

		auto iter = std::find(data->_children.cbegin(), data->_children.cend(), sg);

		if (iter == data->_children.cend())
		{
			sg->_data->_parent = this;

			data->_children.push_back(std::move(sg));
		}
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::disconnect(signal *sg)
	{
		internal_data* data = _data.get();
		std::lock_guard<std::mutex> locker(data->_mutex);

		data->_children.remove(sg);
	}

	template<typename R, typename... Args>
	connection signal<R(Args...)>::connect(const callback_type& fn, slot *owner)
	{
		return create_connection(static_cast<callback_type>(fn), owner);
	}

	template<typename R, typename... Args>
	connection signal<R(Args...)>::connect(callback_type&& fn, slot *owner)
	{
		return create_connection(std::move(fn), owner);
	}

	template<typename R, typename... Args>
	template<typename T, typename U>
	connection signal<R(Args...)>::connect(T *p, const U& fn, slot *owner)
	{
		auto mem_fn = std::move(construct_mem_fn(fn, p, make_int_sequence<sizeof...(Args)>{}));

		return create_connection(std::move(mem_fn), owner);
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::disconnect(const connection& connection)
	{
		destroy_connection(connection._data);
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::disconnect(slot *owner)
	{
		if (owner != nullptr)
		{
			destroy_connection(owner->_data);
		}
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::disconnect_all()
	{
		internal_data* data = _data.get();
		std::lock_guard<std::mutex> locker(data->_mutex);

		for (const auto& jnt : data->_callbacks)
		{
			if (jnt.owner != nullptr)
			{
				jnt.owner->_data = nullptr;
				jnt.owner->_cleaners.clear();
			}
		}
		data->_callbacks.clear();
		for (auto sig : data->_children)
		{
			if (sig->_data->_parent == this) // should be an assert
			{
				sig->_data->_parent = nullptr;
			}
		}
		data->_children.clear();
	}


	template<typename R, typename... Args>
	R signal<R(Args...)>::operator() (Args... args) const
	{
		internal_data* data = _data.get();
		typename std::list<joint>::const_iterator cit_begin, cit_end;

		{
			std::lock_guard<std::mutex> locker(data->_mutex);
			if (data->_locked || data->_signal_called)
				return R();
		}

		std::shared_ptr<internal_data> data_store(_data);
		data->_signal_called = true;

		for (signal *sig : data->_children)
		{
			sig->operator()(std::forward<Args>(args)...);
		}

		if constexpr (std::is_same<R, void>::value)
		{
			for (auto iter = data->_callbacks.cbegin(); iter != data->_callbacks.cend(); ++iter)
			{
				const joint& jnt = *iter;

				if (!jnt.connection->locked)
				{
					jnt.callback(std::forward<Args>(args)...);
				}
			}

			data->_signal_called = false;
			add_and_delete_deffered();
			return;
		} else
		{
			R r;
			for (auto iter = data->_callbacks.cbegin(); iter != data->_callbacks.cend(); ++iter)
			{
				const joint& jnt = *iter;

				if (!jnt.connection->locked)
				{
					if (std::next(iter, 1) == data->_callbacks.cend())
					{
						r = jnt.callback(std::forward<Args>(args)...);
						break;
					}

					jnt.callback(std::forward<Args>(args)...);
				}
			}

			data->_signal_called = false;
			add_and_delete_deffered();
			return r;
		}
	}

	template<typename R, typename... Args>
	template<typename T>
	R signal<R(Args...)>::operator() (Args... args, const T& agg) const
	{
		internal_data* data = _data.get();
		std::vector<R> result;

		std::lock_guard<std::mutex> locker(data->_mutex);

		if (!data->_locked)
		{
			for (signal *sig : data->_children)
			{
				sig->operator()(std::forward<Args>(args)...);
			}

			result.reserve(data->_callbacks.size());

			for (auto iter = data->_callbacks.cbegin(); iter != data->_callbacks.cend(); ++iter)
			{
				const joint& jnt = *iter;

				if (!jnt.connection->locked)
				{
					result.push_back(std::move(jnt.callback(std::forward<Args>(args)...)));
				}
			}
		}

		return agg(std::move(result));
	}

	template<typename R, typename... Args>
	template<typename T, typename U, int... Ns>
	typename signal<R(Args...)>::callback_type signal<R(Args...)>::construct_mem_fn(const T& fn, U *p, int_sequence<Ns...>) const
	{
		return std::bind(fn, p, placeholder_lsignal<Ns>{}...);
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::copy_callbacks(const std::list<joint>& callbacks)
	{
		internal_data* data = _data.get();
		data->_callbacks.clear();
		for (auto iter = callbacks.begin(); iter != callbacks.end(); ++iter)
		{
			const joint& jn = *iter;

			if (jn.owner == nullptr)
			{
				joint jnt;

				jnt.callback = jn.callback;
				jnt.connection = jn.connection;
				jnt.owner = nullptr;

				data->_callbacks.push_back(std::move(jnt));
			}
		}
	}

	template<typename R, typename... Args>
	std::shared_ptr<connection_data> signal<R(Args...)>::create_connection(callback_type&& fn, slot *owner)
	{
		std::shared_ptr<connection_data> connection = std::make_shared<connection_data>();

		if (owner != nullptr)
		{
			auto deleter = [this](std::shared_ptr<connection_data> connection)
			{
				destroy_connection(connection);
			};

			connection_cleaner cleaner;

			cleaner.deleter = deleter;
			cleaner.data = connection;

			owner->_data = connection;
			owner->_cleaners.emplace_back(cleaner);
		}

		joint jnt;

		jnt.callback = std::move(fn);
		jnt.connection = connection;
		jnt.owner = owner;

		internal_data* data = _data.get();
		std::lock_guard<std::mutex> locker(data->_mutex);

		if(data->_signal_called)
			data->_deffered_add_connection.push_back(std::move(jnt));
		else
			data->_callbacks.push_back(std::move(jnt));

		return connection;
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::destroy_connection(std::shared_ptr<connection_data> connection)
	{
		internal_data* data = _data.get();
		std::lock_guard<std::mutex> locker(data->_mutex);
		if(data->_signal_called)
		{
			data->_deffered_delete_connection.push_back(connection);
			return;
		}

		destroy_connection_internal(connection);
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::destroy_connection_internal(std::shared_ptr<connection_data>& connection) const
	{
		internal_data* data = _data.get();
		for (auto iter = data->_callbacks.begin(); iter != data->_callbacks.end(); ++iter)
		{
			const joint& jnt = *iter;

			if (jnt.connection == connection)
			{
				if (jnt.owner != nullptr)
				{
					jnt.owner->_data = nullptr;
					jnt.owner->_cleaners.clear();
				}

				data->_callbacks.erase(iter);
				break;
			}
		}
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::add_and_delete_deffered() const
	{
		internal_data* data = _data.get();
		std::lock_guard<std::mutex> locker(data->_mutex);
		for(const joint& jnt : data->_deffered_add_connection)
		{
			data->_callbacks.push_back(jnt);
		}

		data->_deffered_add_connection.clear();

		for(auto& conn : data->_deffered_delete_connection)
		{
			destroy_connection_internal(conn);
		}

		data->_deffered_delete_connection.clear();
	}

	template<typename R, typename... Args>
	bool signal<R(Args...)>::empty() const
	{
		internal_data* data = _data.get();
		std::lock_guard<std::mutex> locker(data->_mutex);
		return data->_callbacks.empty();
	}
}
