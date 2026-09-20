// Implementation for lsignal.h.
//
// Everything the public header used to need <atomic>/<memory>/<mutex>/
// <vector> for lives here instead: connection_data and signal_impl are
// intrusively refcounted (plain std::atomic<int>, no shared_ptr control
// block), the callback list is a hand-rolled intrusive singly-linked list,
// and slot's mutex+cleaner-vector live behind a pimpl (detail::slot_impl).
#include "lsignal.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <new>
#include <vector>

namespace lsignal
{
	struct connection_data
	{
		std::atomic<bool> locked{false};
		std::atomic<bool> deleted{false};
		//Starts at 1: the reference implicitly owned by whichever caller
		//just allocated this object (see signal_base::add_callback) - every
		//other owner (the returned connection, an owner slot's cleaner
		//list, a copied signal's list node) explicitly addrefs.
		std::atomic<int> refcount{1};
	};

	//Internal-linkage helpers (deliberately plain `static`): this is the one
	//and only translation unit that ever sees them.
	static void addref(connection_data* d) noexcept
	{
		if (d)
			d->refcount.fetch_add(1, std::memory_order_relaxed);
	}

	static void release(connection_data* d) noexcept
	{
		if (d && d->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			delete d;
	}

	connection::connection() noexcept
		: _data(nullptr)
	{
	}

	connection::connection(connection_data* adopt) noexcept
		: _data(adopt)
	{
	}

	connection::connection(const connection& rhs) noexcept
		: _data(rhs._data)
	{
		addref(_data);
	}

	connection::connection(connection&& rhs) noexcept
		: _data(rhs._data)
	{
		rhs._data = nullptr;
	}

	connection& connection::operator= (const connection& rhs) noexcept
	{
		if (this == &rhs)
			return *this;
		//addref rhs before releasing our own - safe even if rhs._data and
		//_data happen to be the same object.
		addref(rhs._data);
		release(_data);
		_data = rhs._data;
		return *this;
	}

	connection& connection::operator= (connection&& rhs) noexcept
	{
		if (this == &rhs)
			return *this;
		release(_data);
		_data = rhs._data;
		rhs._data = nullptr;
		return *this;
	}

	connection::~connection()
	{
		release(_data);
	}

	bool connection::is_locked() const noexcept
	{
		return _data && _data->locked.load();
	}

	void connection::set_lock(const bool lock) noexcept
	{
		if (_data)
			_data->locked = lock;
	}

	void connection::disconnect() noexcept
	{
		if (_data)
		{
			//connection fully cleared after next signal call or signal delete
			_data->deleted = true;
			release(_data);
			_data = nullptr;
		}
	}

	namespace detail
	{
		struct slot_impl
		{
			std::mutex mutex;
			std::vector<connection_data*> cleaners;
		};
	}

	slot::slot()
		: _impl(new detail::slot_impl())
	{
	}

	slot::~slot()
	{
		disconnect();
		delete _impl;
	}

	void slot::disconnect()
	{
		std::vector<connection_data*> cleaners;
		{
			std::lock_guard<std::mutex> locker(_impl->mutex);
			cleaners.swap(_impl->cleaners);
		}

		for (connection_data* d : cleaners)
		{
			d->deleted = true;
			release(d);
		}
	}

	namespace detail
	{
		// One callback slot, independent of any signal's R/Args. ctx points
		// at the connected callable's storage, which lives inline in the tail
		// of this same node's allocation block (see allocate_node below) -
		// one allocation covers both the node and the callable, not two.
		// destroy is a plain function pointer rather than going through
		// shared_ptr<void>/unique_ptr<void, fn>: this way the node itself,
		// not a smart pointer type, is the only thing that needs to stay a
		// single shared type across every signal signature - see
		// test_debug_information/README.md for why that matters for debug
		// info (shared_ptr's control block, or even unique_ptr's deleter
		// slot, would otherwise still be a per-connected-callable-type cost).
		//
		// clone exists because copy_from (signal copy) deep-copies each
		// connected callable's storage (independent closures after a signal
		// copy, only the connection_data is shared) - it needs an explicit
		// per-F clone function since ctx is an untyped void*. It placement-
		// clones into a caller-provided destination rather than allocating,
		// matching the no-separate-allocation-for-ctx scheme above.
		//
		// The list is a hand-rolled intrusive singly-linked list, not
		// std::list<node> - the whole point of signal_impl being opaque is
		// that code outside this file only ever holds a bare node* cursor
		// (see emit_scope below), and a std::list::iterator can't be safely
		// round-tripped through a void*-sized cursor. It preserves the same
		// guarantee std::list gave: appending (link()) never invalidates or
		// moves any existing node, which lets connect() run from one thread
		// while operator() iterates concurrently in another, unsynchronized
		// with each other (see emit_scope::next()'s comment for the precise
		// invariant that makes this race-free).
		class signal_impl
		{
		public:
			//Fields every emission touches (via emit_scope::next(), see
			//below) come first, so they share the block's first cache line
			//with the head of the connected callable's own storage right
			//after it; ctx_size/ctx_align are only read by copy_from/
			//deallocate_node_block, not by the hot emit path.
			struct node
			{
				node* next = nullptr;
				void* ctx = nullptr;              //points into this node's own tail, see allocate_node_block
				invoke_fn invoke = nullptr;
				connection_data* connection = nullptr;
				void (*destroy)(void*) = nullptr;
				void (*clone)(void*, const void*) = nullptr;
				unsigned ctx_size = 0;
				unsigned ctx_align = 0;
			};
			//Load-bearing for deallocate_node_block, which frees the block
			//without calling ~node() - see there.
			static_assert(std::is_trivially_destructible_v<node>);

#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
			static constexpr std::size_t default_new_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
			static constexpr std::size_t default_new_alignment = alignof(std::max_align_t);
#endif

			static std::size_t round_up(std::size_t size, std::size_t align)
			{
				return (size + align - 1) & ~(align - 1);
			}

			//Allocates one block covering both the node and inline storage
			//for the connected callable (ctx_size bytes, aligned to
			//ctx_align) right after it - merges what used to be two
			//allocations (the node, and `new DF(...)` in create_connection)
			//into one, see test_debug_information/README.md. Constructs the
			//node in place (this returns a live object, not raw bytes) and
			//writes the callable storage's address to *ctx_out; the callable
			//itself is not constructed here - the caller placement-news it
			//before the node is handed to add_callback.
			static node* allocate_node_block(unsigned ctx_size, unsigned ctx_align, void** ctx_out)
			{
				const std::size_t block_align = alignof(node) > ctx_align ? alignof(node) : ctx_align;
				const std::size_t ctx_offset = round_up(sizeof(node), ctx_align);
				const std::size_t total = ctx_offset + ctx_size;

				void* raw = (block_align > default_new_alignment)
					? ::operator new(total, std::align_val_t(block_align))
					: ::operator new(total);

				node* n = ::new (raw) node();
				n->ctx = static_cast<char*>(raw) + ctx_offset;
				n->ctx_size = ctx_size;
				n->ctx_align = ctx_align;
				*ctx_out = n->ctx;
				return n;
			}

			//Frees a block allocated by allocate_node_block, with the exact
			//same operator-new/delete form (plain vs aligned) it was
			//allocated with - never calls ~node() (see the static_assert
			//above) and never calls destroy() on the callable: the caller is
			//responsible for that first if the callable was constructed.
			static void deallocate_node_block(node* n) noexcept
			{
				const std::size_t block_align = alignof(node) > n->ctx_align ? alignof(node) : n->ctx_align;
				if (block_align > default_new_alignment)
					::operator delete(n, std::align_val_t(block_align));
				else
					::operator delete(n);
			}

			mutable std::mutex mutex;
			std::atomic<bool> locked{false};
			//Starts at 1: the reference implicitly owned by the signal_base
			//that just allocated this object - emit_scope explicitly
			//addrefs/releases its own temporary reference around each
			//emission (so a callback can safely delete the owning signal
			//mid-emission without invalidating the emission in progress).
			std::atomic<int> refcount{1};
			int emit_count = 0;              // guarded entirely by `mutex`, like the old _signal_called_count
			node* head = nullptr;
			node* tail = nullptr;

			~signal_impl()
			{
				clear();
			}

			bool empty() const
			{
				std::lock_guard<std::mutex> locker(mutex);
				return head == nullptr;
			}

			void disconnect_all()
			{
				std::lock_guard<std::mutex> locker(mutex);
				for (node* n = head; n; n = n->next)
					n->connection->deleted = true;
				//don't clear the list, only mark deleted - same as before
			}

			//caller must hold mutex
			void clear()
			{
				node* n = head;
				while (n)
				{
					node* next_n = n->next;
					//A node only ever reaches this list already fully
					//constructed (add_callback), so ctx is always live here.
					n->destroy(n->ctx);
					release(n->connection);
					deallocate_node_block(n);
					n = next_n;
				}
				head = tail = nullptr;
			}

			//caller must hold mutex
			void prune()
			{
				node* prev = nullptr;
				node* n = head;
				while (n)
				{
					node* next_n = n->next;
					if (n->connection->deleted)
					{
						if (prev) prev->next = next_n; else head = next_n;
						if (n == tail) tail = prev;
						n->destroy(n->ctx);
						release(n->connection);
						deallocate_node_block(n);
					}
					else
					{
						prev = n;
					}
					n = next_n;
				}
			}

			//caller must hold mutex; n must be fully initialized already
			void link(node* n)
			{
				n->next = nullptr;
				if (tail) tail->next = n; else head = n;
				tail = n;
			}

			//caller must hold both this->mutex and rhs.mutex; *this must be
			//empty. If a clone() throws partway through (e.g. the connected
			//callable's copy constructor throws), the nodes already linked
			//stay linked - the caller is responsible for freeing *this
			//(hence the whole node list) on failure, see signal_base's copy
			//constructor.
			void copy_from(const signal_impl& rhs)
			{
				for (node* n = rhs.head; n; n = n->next)
				{
					void* dst_ctx;
					node* dst = allocate_node_block(n->ctx_size, n->ctx_align, &dst_ctx);
					try
					{
						n->clone(dst_ctx, n->ctx);
					}
					catch (...)
					{
						deallocate_node_block(dst); //ctx was never constructed - nothing to destroy
						throw;
					}
					dst->destroy = n->destroy;
					dst->clone = n->clone;
					dst->invoke = n->invoke;
					dst->connection = n->connection; //shared, not cloned
					addref(dst->connection);
					link(dst);
				}
			}
		};

		//Internal-linkage helper for signal_impl's own refcount - see the
		//connection_data addref/release comment above for why `static`.
		static void release_signal_impl(signal_impl* d) noexcept
		{
			if (d && d->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1)
				delete d;
		}

		emit_scope::emit_scope(signal_impl* d) noexcept
			: _d(d)
		{
			if (!d)
			{
				_active = false;
				return;
			}

			d->refcount.fetch_add(1, std::memory_order_relaxed);

			std::lock_guard<std::mutex> locker(d->mutex);
			if (d->emit_count == 0)
				d->prune();

			if (d->locked || d->head == nullptr)
			{
				_active = false;
				return;
			}

			d->emit_count++;
			_cur = d->head;
			_last = d->tail;
			_active = true;
		}

		emit_scope::~emit_scope()
		{
			if (!_d)
				return;

			{
				std::lock_guard<std::mutex> locker(_d->mutex);
				if (_active)
					_d->emit_count--;
			}
			//Decrement the emission count and unlock before releasing: if
			//this is the reference keeping signal_impl alive (the owning
			//signal was deleted from inside a callback during this
			//emission), release() may delete _d, including its mutex -
			//that must happen after we're done touching it.
			release_signal_impl(_d);
		}

		bool emit_scope::next(void*& ctx, invoke_fn& fn) noexcept
		{
			using node = signal_impl::node;
			while (_cur)
			{
				node* n = static_cast<node*>(_cur);
				//Terminate on node IDENTITY with the snapshotted last node,
				//never on a null `next` pointer: a concurrent connect() (see
				//signal_impl::link above) can only ever write `_last->next`
				//(the previous last node's `next` field), and this loop is
				//the only reader that could race it - so it must never read
				//that specific field. Every other node's `next` is immutable
				//for the duration of this emission (the only thing that
				//unlinks nodes, prune(), is gated on emit_count == 0). This
				//is the exact invariant that made std::list safe here too.
				const bool is_last = (n == static_cast<node*>(_last));
				_cur = is_last ? nullptr : n->next;

				if (!n->connection->locked && !n->connection->deleted && n->invoke)
				{
					ctx = n->ctx;
					fn = n->invoke;
					return true;
				}

				if (is_last)
					break;
			}
			return false;
		}

		signal_base::signal_base()
			: _data(new signal_impl())
		{
		}

		signal_base::~signal_base()
		{
			release_signal_impl(_data);
		}

		signal_base::signal_base(const signal_base& rhs)
			: _data(new signal_impl())
		{
			//Unlike a std::shared_ptr<signal_impl>, a raw _data isn't
			//automatically freed if the body below throws partway through
			//copy_from (e.g. a connected callable's copy ctor throws) -
			//release it manually so a failed signal copy doesn't leak.
			try
			{
				signal_impl* data = _data;
				signal_impl* rhs_data = rhs._data;

				std::unique_lock<std::mutex> lock_own(data->mutex, std::defer_lock);
				std::unique_lock<std::mutex> lock_rhs(rhs_data->mutex, std::defer_lock);
				std::lock(lock_own, lock_rhs);

				//rhs signal may be in the middle of being emitted (iterating
				//its list outside the mutex, see emit_scope); pruning
				//deleted connections here would invalidate that iteration,
				//so defer it just like a fresh emission does.
				if (rhs_data->emit_count == 0)
					rhs_data->prune();

				data->locked = rhs_data->locked.load();
				data->copy_from(*rhs_data);
			}
			catch (...)
			{
				release_signal_impl(_data);
				throw;
			}
		}

		signal_base& signal_base::operator= (const signal_base& rhs)
		{
			if (this == &rhs)
				return *this;

			signal_impl* data = _data;
			signal_impl* rhs_data = rhs._data;

			std::unique_lock<std::mutex> lock_own(data->mutex, std::defer_lock);
			std::unique_lock<std::mutex> lock_rhs(rhs_data->mutex, std::defer_lock);
			std::lock(lock_own, lock_rhs);

			//see signal_base(const signal_base&) above for why this is deferred
			if (rhs_data->emit_count == 0)
				rhs_data->prune();

			data->locked = rhs_data->locked.load();

			data->clear();
			data->copy_from(*rhs_data);

			return *this;
		}

		signal_base::signal_base(signal_base&& rhs) noexcept
			: _data(rhs._data)
		{
			rhs._data = nullptr;
		}

		signal_base& signal_base::operator= (signal_base&& rhs) noexcept
		{
			if (this == &rhs)
				return *this;
			release_signal_impl(_data);
			_data = rhs._data;
			rhs._data = nullptr;
			return *this;
		}

		bool signal_base::is_locked() const noexcept
		{
			return _data->locked;
		}

		void signal_base::set_lock(const bool lock) noexcept
		{
			_data->locked = lock;
		}

		void signal_base::disconnect(const connection& conn) const noexcept
		{
			const_cast<connection*>(&conn)->disconnect();
		}

		void signal_base::disconnect_all()
		{
			_data->disconnect_all();
		}

		bool signal_base::empty() const
		{
			return _data->empty();
		}

		void* signal_base::alloc_node(unsigned ctx_size, unsigned ctx_align, void** ctx_out)
		{
			return signal_impl::allocate_node_block(ctx_size, ctx_align, ctx_out);
		}

		void signal_base::free_node(void* node) noexcept
		{
			signal_impl::deallocate_node_block(static_cast<signal_impl::node*>(node));
		}

		connection signal_base::add_callback(void* node, void (*destroy)(void*),
			void (*clone)(void*, const void*), invoke_fn invoke, slot* owner)
		{
			using node_t = signal_impl::node;
			node_t* raw = static_cast<node_t*>(node);

			//raw->ctx already points at the constructed callable (see
			//alloc_node/create_connection) - destroy/clone/invoke must be
			//assigned before node_guard below is installed, since the guard
			//unconditionally calls destroy(ctx) on unwind.
			raw->destroy = destroy;
			raw->clone = clone;
			raw->invoke = invoke;

			//Cleans up the node - and, through it, the callable and (once
			//assigned below) its connection_data reference - if anything
			//past this point throws before ownership is fully handed off to
			//the list.
			struct node_guard
			{
				node_t* n;
				~node_guard()
				{
					if (n)
					{
						n->destroy(n->ctx);
						release(n->connection);
						signal_impl::deallocate_node_block(n);
					}
				}
			} guard{raw};

			//connection_data's refcount starts at 1 (this node's own
			//reference, see its declaration) - every other owner below
			//explicitly addrefs.
			raw->connection = new connection_data();

			if (owner != nullptr)
			{
				addref(raw->connection);
				try
				{
					std::lock_guard<std::mutex> locker(owner->_impl->mutex);
					owner->_impl->cleaners.push_back(raw->connection);
				}
				catch (...)
				{
					release(raw->connection); //undo the addref above
					throw;                    //node_guard releases the node's own ref and frees raw/ctx
				}
			}

			addref(raw->connection); //the reference the returned connection adopts below

			{
				std::lock_guard<std::mutex> locker(_data->mutex);
				_data->link(raw);
			}

			connection_data* conn_data = raw->connection;
			guard.n = nullptr; //ownership (node, ctx, the node's connection_data ref) transferred to the list
			return connection(conn_data);
		}
	}
}
