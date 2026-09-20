/*

The MIT License (MIT)

Copyright (c) 2015 Ievgen Polyvanyi, 2019 Dmitriy Poskryakov

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

#include "lsignal_defines.h"

#include <type_traits>
#include <utility>

// This is the split, three-file form of lsignal - see lsignal.cpp for the
// implementation and ../lsignal_header_only/lsignal.h for the alternative,
// original single-file form (root README.md's "lsignal_with_cpp vs
// lsignal_header_only" section compares them). connection_data,
// detail::signal_impl and detail::slot_impl are opaque here - forward-declared
// or not mentioned at all - and fully defined in lsignal.cpp: the mutex,
// atomic refcounts, callback list and cleaner list they hold never need to be
// visible to (or instantiated by) a consumer TU. That's what lets this header
// need nothing beyond <type_traits>/<utility> (for std::decay_t/void_t/
// declval/forward - see detail::has_bool_conversion and create_connection
// below). See test_debug_information/README.md for the per-stage measurements
// that motivated this and bugs.md for the ownership/refcounting invariants
// lsignal.cpp relies on.
//
// LSIGNAL_DLL (lsignal_defines.h) marks the classes below whose non-inline
// methods or vtable are reached from template code that gets instantiated in
// a *consumer* TU (signal<R(Args...)> inherits detail::signal_base and drives
// detail::emit_scope directly) - if lsignal.cpp is ever built into its own
// shared library, those need to cross the DLL boundary. connection_data/
// detail::signal_impl/detail::slot_impl need no such marking: they're only
// ever touched from code that also lives inside lsignal.cpp.
namespace lsignal
{
	// Opaque; refcounted (see lsignal.cpp). Shared by up to four
	// owners at once: the connection object handed to connect()'s caller
	// (and its copies), the node in the signal's callback list (and, after a
	// signal copy, the copy's node too - copies share connection_data,
	// that's what lets disconnecting through either side affect both), and
	// the owner slot's cleaner list.
	struct connection_data;

	namespace detail { class signal_impl; class signal_base; struct slot_impl; }

	class LSIGNAL_DLL connection
	{
		friend class detail::signal_base;

	public:
		connection() noexcept;
		connection(const connection& rhs) noexcept;
		connection(connection&& rhs) noexcept;
		connection& operator= (const connection& rhs) noexcept;
		connection& operator= (connection&& rhs) noexcept;
		virtual ~connection();

		bool is_locked() const noexcept;
		void set_lock(const bool lock) noexcept;

		void disconnect() noexcept;
	private:
		//Adopts one reference already held by the caller (add_callback) -
		//does not addref.
		explicit connection(connection_data* adopt) noexcept;

		connection_data* _data;
	};


	// slot
	class LSIGNAL_DLL slot
	{
		friend class detail::signal_base;
	public:
		slot();
		virtual ~slot();

		//Was implicitly non-copyable/non-movable while _mutex was a direct
		//std::mutex member; a pimpl'd slot would otherwise silently become
		//copyable (two slots sharing one impl -> double free), so this has
		//to be explicit now.
		slot(const slot&) = delete;
		slot& operator= (const slot&) = delete;

		void disconnect();
	private:
		detail::slot_impl* _impl;
	};

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

		// reinterpret_cast'd to R(*)(void*, Args...) at the one call site in
		// signal<R(Args...)>::operator() - the same erased shape connect()
		// has always stored the target callable's dispatch behind.
		using invoke_fn = void(*)();

		// Non-template RAII emission cursor - the entire per-signature cost
		// of walking the callback list, replacing what used to be inline
		// iterator logic in signal::operator(). Constructing it does what
		// operator() used to do under the mutex (prune if idle, bail out if
		// locked/empty, bump the emission count, snapshot the [head, tail]
		// range being iterated - callbacks connected after this point are
		// not visited by this emission), and it keeps signal_impl alive for
		// its own lifetime (an addref'd reference, released in the
		// destructor) so a callback that deletes the owning signal doesn't
		// invalidate the emission in progress. next() walks forward,
		// skipping locked/deleted/null-invoke entries; the destructor
		// decrements the emission count on scope exit, including when a
		// callback throws (the old emit_guard behaviour).
		class LSIGNAL_DLL emit_scope
		{
		public:
			explicit emit_scope(signal_impl* d) noexcept;
			~emit_scope();

			emit_scope(const emit_scope&) = delete;
			emit_scope& operator=(const emit_scope&) = delete;

			bool next(void*& ctx, invoke_fn& fn) noexcept;
		private:
			signal_impl* _d;
			void* _cur = nullptr;
			void* _last = nullptr;
			bool _active = false;
		};

		// Non-template twin of the old per-signature internal_data, plus the
		// bookkeeping (disconnect_all/empty/is_locked/set_lock/add_callback)
		// that doesn't need to know R/Args. Every signal<R(Args...)>
		// instantiation inherits this one compiled class instead of
		// re-instantiating its own copy.
		class LSIGNAL_DLL signal_base
		{
		public:
			bool is_locked() const noexcept;
			void set_lock(const bool lock) noexcept;
			void disconnect(const connection& conn) const noexcept;
			void disconnect_all();
			bool empty() const;

		protected:
			signal_base();
			~signal_base();

			signal_base(const signal_base& rhs);
			signal_base& operator= (const signal_base& rhs);

			signal_base(signal_base&& rhs) noexcept;
			signal_base& operator= (signal_base&& rhs) noexcept;

			// Takes ownership of ctx unconditionally: if anything below
			// throws, destroy(ctx) runs before the exception propagates.
			// invoke may be null (an empty std::function connected directly,
			// see has_bool_conversion above) - the emit loop then skips this
			// entry forever, exactly like the old per-call `if (jnt.callback)`.
			connection add_callback(void* ctx, void (*destroy)(void*),
				void* (*clone)(const void*), invoke_fn invoke, slot* owner);

			signal_impl* _data;
		};
	}

	// signal
	template<typename>
	class signal;

	template<typename R, typename... Args>
	class signal<R(Args...)> : public detail::signal_base
	{
	public:
		using result_type = R;

		// No special members declared here: all six (default/copy/move ctor,
		// copy/move assign, destructor) are implicit and simply forward to
		// detail::signal_base's. Declaring so much as a destructor here would
		// silently suppress the implicit move ctor/assign and degrade a move
		// to a (perfectly legal but surprising) deep copy - see bugs.md.

		//Connects any callable whose signature matches R(Args...): a free
		//function, a lambda, a functor, or a std::function (including an
		//empty one, which is accepted but silently skipped on emission).
		template<typename F>
		connection connect(F&& fn, slot* owner);

		template<typename T, typename Tfn>
		connection connect(T* p, R(Tfn::*fn)(Args...), slot* owner);

		template<typename T, typename Tfn>
		connection connect(T* p, R(Tfn::*fn)(Args...) const, slot* owner);

		//Return last called signal result.
		R operator() (Args... args) const;
	private:
		using raw_invoke_t = R(*)(void*, Args...);

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
		connection create_connection(F&& fn, slot* owner)
		{
			using DF = std::decay_t<F>;
			DF* raw = new DF(std::forward<F>(fn));

			detail::invoke_fn invoke = reinterpret_cast<detail::invoke_fn>(&invoke_thunk<DF>);
			if constexpr (detail::has_bool_conversion<DF>::value)
			{
				if (!static_cast<bool>(*raw))
					invoke = nullptr;
			}

			return this->add_callback(raw, &destroy_ctx<DF>, &clone_ctx<DF>, invoke, owner);
		}
	};

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
	R signal<R(Args...)>::operator() (Args... args) const
	{
		detail::emit_scope scope(_data);

		void* ctx;
		detail::invoke_fn fn;
		if constexpr (std::is_same<R, void>::value)
		{
			while (scope.next(ctx, fn))
				reinterpret_cast<raw_invoke_t>(fn)(ctx, args...);
			return;
		}
		else
		{
			R r{};
			while (scope.next(ctx, fn))
				r = reinterpret_cast<raw_invoke_t>(fn)(ctx, args...);
			return r;
		}
	}
}
