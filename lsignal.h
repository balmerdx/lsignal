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

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>
#include <algorithm>

// Design note: everything a signal<R(Args...)> needs that does not actually
// depend on R/Args - the mutex, the "locked"/emission-count bookkeeping and
// the callback list itself - lives in the non-template lsignal::detail::signal_impl
// below, and each connected callable is stored via a small hand-rolled type
// erasure (erased_entry) instead of std::function<R(Args...)>. The template
// signal<R(Args...)> is a thin typed wrapper around one shared signal_impl.
//
// This exists to keep the debug info this header generates from growing with
// the number of distinct signal<...> signatures in a program: with the
// previous fully-templated design, internal_data/joint/std::function<R(Args...)>
// were all nested in the template and got re-instantiated (and re-emitted
// into every .debug_info) for every signature and in every translation unit
// that used it. See test_debug_information/README.md for the measurements
// that motivated this (roughly 25x less debug info per signature, 2x less
// per connect() - measured with clang++-20 / DWARF5) and the reasoning behind
// the specific choices below (std::list, not std::vector; unique_ptr, not
// shared_ptr, for the erased callable).
namespace lsignal
{
	// connection
	struct connection_data
	{
		std::atomic<bool> locked{false};
		std::atomic<bool> deleted{false};

		connection_data();
		~connection_data();
	};

	struct connection_cleaner
	{
		std::shared_ptr<connection_data> data;

		connection_cleaner();
		~connection_cleaner();
	};

	namespace detail { class signal_impl; }

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
	};


	// slot
	class slot
	{
		template<typename>
		friend class signal;
		friend class detail::signal_impl;
	public:
		slot();
		virtual ~slot();

		void disconnect();
	private:
		mutable std::mutex _mutex;
		std::vector<connection_cleaner> _cleaners;
	};

	inline connection_data::connection_data()
	{

	}

	inline connection_data::~connection_data()
	{

	}

	inline connection_cleaner::connection_cleaner()
	{

	}

	inline connection_cleaner::~connection_cleaner()
	{

	}

	inline connection::connection()
	{

	}

	inline connection::connection(std::shared_ptr<connection_data>&& data)
		: _data(std::move(data))
	{
	}

	inline connection::~connection()
	{
	}

	inline bool connection::is_locked() const
	{
		return _data && _data->locked;
	}

	inline void connection::set_lock(const bool lock)
	{
		if (_data)
			_data->locked = lock;
	}

	inline void connection::disconnect()
	{
		if (_data)
		{
			//connection fully cleared after next signal call or signal delete
			_data->deleted = true;
			_data.reset();
		}
	}

	inline slot::slot()
	{
	}

	inline slot::~slot()
	{
		disconnect();
	}

	inline void slot::disconnect()
	{
		decltype(_cleaners) cleaners;
		{
			std::lock_guard<std::mutex> locker(_mutex);
			cleaners = _cleaners;
			_cleaners.clear();
		}

		for (auto iter = cleaners.cbegin(); iter != cleaners.cend(); ++iter)
		{
			const connection_cleaner& cleaner = *iter;
			cleaner.data->deleted = true;
		}
	}

	namespace detail
	{
		// Detects "F has an explicit operator bool" (true for std::function,
		// false for an ordinary lambda/functor closure type). Used to
		// replicate the historical `if (callback)` truthiness check: an
		// empty std::function connected directly is silently skipped on
		// every emission instead of throwing std::bad_function_call.
		template<typename T, typename = void>
		struct has_bool_conversion : std::false_type {};
		template<typename T>
		struct has_bool_conversion<T, std::void_t<decltype(static_cast<bool>(std::declval<const T&>()))>>
			: std::true_type {};

		// One callback slot, independent of any signal's R/Args - the whole
		// point is that this type is compiled exactly once for the whole
		// program, not once per lsignal::signal<...> signature.
		//
		// ctx owns the connected callable via unique_ptr<void, fn-ptr-deleter>
		// rather than shared_ptr<void>: shared_ptr's control block
		// (_Sp_counted_ptr_inplace<F,...>, with its vtable and RTTI type_info
		// for F) would be instantiated per connected callable type and costs
		// more debug info than the callable itself - more than std::function's
		// own manager function did. unique_ptr with a plain function-pointer
		// deleter has no control block at all: the only per-F cost is the
		// deleter/clone/invoke free functions below, the same shape as
		// std::function's per-target manager function.
		//
		// clone exists because copy_callbacks deep-copies each connected
		// callable (independent closures after a signal copy, only the
		// connection_data is shared) - unique_ptr can't be copied, so cloning
		// needs an explicit per-F clone function.
		struct erased_entry
		{
			using ctx_ptr = std::unique_ptr<void, void(*)(void*)>;

			ctx_ptr ctx{nullptr, [](void*) {}};   // owns the connected callable
			void* (*clone)(const void*) = nullptr;
			void (*invoke)() = nullptr;           // reinterpret_cast'd R(*)(void*, Args...)
			std::shared_ptr<connection_data> connection;
		};

		// Non-template twin of the old per-signature internal_data, plus the
		// bookkeeping methods that don't need to know R/Args (disconnect_all,
		// the deferred erase pass, copy_callbacks, add_cleaner). Every
		// signal<R(Args...)> instantiation shares this one compiled class.
		//
		// The callback list stays std::list, not std::vector: some callers
		// connect() from one thread while another thread is mid-emission,
		// unsynchronized with each other, relying on the guarantee that
		// push_back never invalidates or moves existing nodes. std::vector
		// can reallocate its whole backing store on push_back, which would
		// turn that into a real data race. This doesn't cost anything for the
		// debug-info goal either: once the stored element type (erased_entry)
		// stopped depending on R/Args, std::list<erased_entry> already became
		// a single shared type across every signal signature on its own.
		class signal_impl
		{
		public:
			mutable std::mutex _mutex;
			std::atomic<bool> _locked{false};
			int _signal_called_count = 0;
			std::list<erased_entry> _callbacks;

			bool is_locked() const { return _locked; }
			void set_lock(const bool lock) { _locked = lock; }

			bool empty() const
			{
				std::lock_guard<std::mutex> locker(_mutex);
				return _callbacks.empty();
			}

			void disconnect_all()
			{
				std::lock_guard<std::mutex> locker(_mutex);
				for (auto& e : _callbacks)
					e.connection->deleted = true;
				//_callbacks.clear(); dont clear callbacks, only mark deleted
			}

			//caller must hold _mutex
			void delete_deferred_internal()
			{
				auto it_to_remove = std::remove_if(_callbacks.begin(), _callbacks.end(),
					[](const erased_entry& e) -> bool { return e.connection->deleted; });
				_callbacks.erase(it_to_remove, _callbacks.end());
			}

			void copy_callbacks(const std::list<erased_entry>& callbacks)
			{
				_callbacks.clear();
				for (auto iter = callbacks.begin(); iter != callbacks.end(); ++iter)
				{
					const erased_entry& src = *iter;
					erased_entry dst;
					dst.ctx = erased_entry::ctx_ptr(src.clone(src.ctx.get()), src.ctx.get_deleter());
					dst.clone = src.clone;
					dst.invoke = src.invoke;
					dst.connection = src.connection; //shared, not cloned
					_callbacks.push_back(std::move(dst));
				}
			}

			//caller must hold _mutex
			void add_cleaner(slot* owner, std::shared_ptr<connection_data>& connection)
			{
				connection_cleaner cleaner;
				cleaner.data = connection;

				if (owner != nullptr)
				{
					std::lock_guard<std::mutex> locker(owner->_mutex);
					owner->_cleaners.emplace_back(cleaner);
				}
			}
		};
	}

	// signal
	template<typename>
	class signal;

	template<typename R, typename... Args>
	class signal<R(Args...)>
	{
	public:
		using result_type = R;

		signal();
		~signal();

		signal(const signal& rhs);
		signal& operator= (const signal& rhs);

		signal(signal&& rhs) = default;
		signal& operator= (signal&& rhs) = default;

		bool is_locked() const;
		void set_lock(const bool lock);

		//Connects any callable whose signature matches R(Args...): a free
		//function, a lambda, a functor, or a std::function (including an
		//empty one, which is accepted but silently skipped on emission).
		template<typename F>
		connection connect(F&& fn, slot* owner);

		template<typename T, typename Tfn>
		connection connect(T* p, R(Tfn::*fn)(Args...), slot* owner);

		template<typename T, typename Tfn>
		connection connect(T* p, R(Tfn::*fn)(Args...) const, slot* owner);

		void disconnect(const connection& connection);

		void disconnect_all();

		//Return last called signal result.
		R operator() (Args... args) const;

		//this signal don`t have direct connections
		bool empty() const;
	private:
		using raw_invoke_t = R(*)(void*, Args...);

		std::shared_ptr<detail::signal_impl> _data;

		template<typename F>
		static R invoke_thunk(void* ctx, Args... args)
		{
			//std::forward here (not a plain lvalue pass) is what makes the
			//copy/move-count contract match the historical std::function-based
			//behavior exactly: args are this thunk's own by-value copy (made
			//when the emit loop called us with lvalues), so forwarding them
			//as rvalues into the actual target is safe - it can't affect any
			//other connected callback's view of the shared arguments in
			//signal::operator().
			return (*static_cast<F*>(ctx))(std::forward<Args>(args)...);
		}

		template<typename F>
		static void destroy_ctx(void* p) { delete static_cast<F*>(p); }

		template<typename F>
		static void* clone_ctx(const void* p) { return new F(*static_cast<const F*>(p)); }

		template<typename F>
		std::shared_ptr<connection_data> create_connection(F&& fn, slot* owner)
		{
			using DF = std::decay_t<F>;
			DF* raw = new DF(std::forward<F>(fn));
			auto conn = std::make_shared<connection_data>();

			detail::erased_entry entry;
			entry.ctx = detail::erased_entry::ctx_ptr(raw, &destroy_ctx<DF>);
			entry.clone = &clone_ctx<DF>;
			if constexpr (detail::has_bool_conversion<DF>::value)
			{
				//e.g. an empty std::function connected directly: leave invoke
				//null so the emit loop's existing `&& e.invoke` check skips it
				//forever, exactly like the old per-call `if (jnt.callback)`.
				entry.invoke = static_cast<bool>(*raw)
					? reinterpret_cast<void(*)()>(&invoke_thunk<DF>)
					: nullptr;
			}
			else
			{
				entry.invoke = reinterpret_cast<void(*)()>(&invoke_thunk<DF>);
			}
			entry.connection = conn;

			detail::signal_impl* data = _data.get();
			std::lock_guard<std::mutex> locker(data->_mutex);
			data->add_cleaner(owner, conn);
			data->_callbacks.push_back(std::move(entry));
			return conn;
		}
	};

	template<typename R, typename... Args>
	signal<R(Args...)>::signal()
		: _data(std::make_shared<detail::signal_impl>())
	{
	}

	template<typename R, typename... Args>
	signal<R(Args...)>::~signal()
	{
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::disconnect_all()
	{
		_data->disconnect_all();
	}

	template<typename R, typename... Args>
	signal<R(Args...)>::signal(const signal& rhs)
		: _data(std::make_shared<detail::signal_impl>())
	{
		detail::signal_impl* data = _data.get();
		detail::signal_impl* rhs_data = rhs._data.get();

		std::unique_lock<std::mutex> lock_own(data->_mutex, std::defer_lock);
		std::unique_lock<std::mutex> lock_rhs(rhs_data->_mutex, std::defer_lock);

		std::lock(lock_own, lock_rhs);
		//rhs signal may be in the middle of being emitted (iterating
		//_callbacks outside the lock, see operator()); pruning deleted
		//connections here would invalidate that iteration, so defer it
		//just like operator() does.
		if (rhs_data->_signal_called_count == 0)
			rhs_data->delete_deferred_internal();

		data->_locked = rhs_data->_locked.load();

		data->copy_callbacks(rhs_data->_callbacks);
	}

	template<typename R, typename... Args>
	signal<R(Args...)>& signal<R(Args...)>::operator= (const signal& rhs)
	{
		if (this == &rhs)
			return *this;

		detail::signal_impl* data = _data.get();
		detail::signal_impl* rhs_data = rhs._data.get();

		std::unique_lock<std::mutex> lock_own(data->_mutex, std::defer_lock);
		std::unique_lock<std::mutex> lock_rhs(rhs_data->_mutex, std::defer_lock);

		std::lock(lock_own, lock_rhs);
		//see signal(const signal&) above for why this is deferred
		if (rhs_data->_signal_called_count == 0)
			rhs_data->delete_deferred_internal();

		data->_locked = rhs_data->_locked.load();

		data->copy_callbacks(rhs_data->_callbacks);

		return *this;
	}

	template<typename R, typename... Args>
	bool signal<R(Args...)>::is_locked() const
	{
		return _data->is_locked();
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::set_lock(const bool lock)
	{
		_data->set_lock(lock);
	}

	template<typename R, typename... Args>
	template<typename F>
	connection signal<R(Args...)>::connect(F&& fn, slot* owner)
	{
		return create_connection(std::forward<F>(fn), owner);
	}

	template<typename R, typename... Args>
	template<typename T, typename Tfn>
	connection signal<R(Args...)>::connect(T *p, R(Tfn::*fn)(Args...), slot *owner)
	{
		auto mem_fn = [fn, p](Args&&... args){ return (p->*fn)(std::forward<decltype(args)>(args)...); };
		return create_connection(std::move(mem_fn), owner);
	}

	template<typename R, typename... Args>
	template<typename T, typename Tfn>
	connection signal<R(Args...)>::connect(T *p, R(Tfn::*fn)(Args...) const, slot *owner)
	{
		auto mem_fn = [fn, p](Args&&... args) { return (p->*fn)(std::forward<decltype(args)>(args)...); };
		return create_connection(std::move(mem_fn), owner);
	}

	template<typename R, typename... Args>
	void signal<R(Args...)>::disconnect(const connection& conn)
	{
		const_cast<connection*>(&conn)->disconnect();
	}

	template<typename R, typename... Args>
	R signal<R(Args...)>::operator() (Args... args) const
	{
		detail::signal_impl* data = _data.get();

		typename std::list<detail::erased_entry>::const_iterator cfirst, clast;

		{
			std::lock_guard<std::mutex> locker(data->_mutex);
			if (data->_signal_called_count == 0)
				data->delete_deferred_internal();

			if (data->_locked || data->_callbacks.empty())
				return R();

			data->_signal_called_count++;

			cfirst = data->_callbacks.cbegin();
			clast = data->_callbacks.cend();
			--clast;
		}

		std::shared_ptr<detail::signal_impl> data_store(_data);

		//Decrements _signal_called_count on scope exit, including when a
		//callback throws - otherwise an exception would leave the count
		//stuck above zero forever and delete_deferred_internal() would
		//never run again (disconnected connections would pile up in
		//_callbacks and empty() would never report true).
		struct emit_guard
		{
			detail::signal_impl* d;
			~emit_guard()
			{
				std::lock_guard<std::mutex> locker(d->_mutex);
				d->_signal_called_count--;
			}
		} guard{data};

		if constexpr (std::is_same<R, void>::value)
		{
			for (auto iter = cfirst; ; ++iter)
			{
				const detail::erased_entry& e = *iter;

				//args are passed as lvalues to every callback (not forwarded/moved) so
				//that a move-happy callback can't leave later callbacks in this same
				//emission observing a moved-from argument.
				if (!e.connection->locked && !e.connection->deleted && e.invoke)
					reinterpret_cast<raw_invoke_t>(e.invoke)(e.ctx.get(), args...);

				if (iter == clast)
					break;
			}
			return;
		} else
		{
			R r{};
			for (auto iter = cfirst; ; ++iter)
			{
				const detail::erased_entry& e = *iter;

				if (!e.connection->locked && !e.connection->deleted && e.invoke)
					r = reinterpret_cast<raw_invoke_t>(e.invoke)(e.ctx.get(), args...);

				if (iter == clast)
					break;
			}
			return r;
		}
	}

	template<typename R, typename... Args>
	bool signal<R(Args...)>::empty() const
	{
		return _data->empty();
	}
}
