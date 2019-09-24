#include "lsignal.h"

namespace lsignal
{

	connection_cleaner::connection_cleaner()
	{

	}

	connection_cleaner::~connection_cleaner()
	{

	}

	connection::connection(std::shared_ptr<connection_data>&& data)
		: _data(std::move(data))
	{
	}

	connection::~connection()
	{
	}

	bool connection::is_locked() const
	{
		return _data->locked;
	}

	void connection::set_lock(const bool lock)
	{
		_data->locked = lock;
	}

	void connection::disconnect()
	{
		decltype(_cleaners) cleaners = _cleaners;

		for (auto iter = cleaners.cbegin(); iter != cleaners.cend(); ++iter)
		{
			const connection_cleaner& cleaner = *iter;

			cleaner.deleter(cleaner.data);
		}
	}

	slot::slot()
		: connection(std::shared_ptr<connection_data>())
	{
	}

	slot::~slot()
	{
		disconnect();
	}

}//namespace lsignal