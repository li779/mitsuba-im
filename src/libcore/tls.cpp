/*
    This file is part of Mitsuba, a physically based rendering system.

    Copyright (c) 2007-2014 by Wenzel Jakob and others.

    Mitsuba is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License Version 3
    as published by the Free Software Foundation.

    Mitsuba is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <mitsuba/core/tls.h>

#include <algorithm>
#include <mutex>

MTS_NAMESPACE_BEGIN

namespace {

thread_local bool has_thread_table_entry = false;

struct compact_thread_table {
	std::mutex mutex;
	std::vector<int> free_ids;
	std::vector<int> open;

	std::mutex data_mutex;
	std::vector<detail::ThreadLocalBase::ThreadLocalPrivate*> data;

	int alloc_thread() {
		assert(!has_thread_table_entry);
		int next_id;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (free_ids.empty()) {
				next_id = (int) open.size();
				open.push_back(0);
			} else {
				next_id = free_ids.back();
				free_ids.pop_back();
				assert(open[next_id] == 0);
			}
		}
		has_thread_table_entry = true;
		return next_id;
	}
	void free_thread(int id) {
		assert(has_thread_table_entry);
		bool cleanUp = false;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (open[id]) {
				std::cout << "Attempting to clean up " << open[id] << " open thread-local storage spaces for thread idx " << id << std::endl;
				cleanUp = true;
			}
		}
		if (cleanUp) {
			try {
				detail::destroyLocalTLS();
			} catch (...) {
				std::cout << "Error during cleanup for thread idx " << id << std::endl;
			}
		}
		has_thread_table_entry = false;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!open[id]) {
				free_ids.push_back(id);
			} else {
				std::cout << open[id] << " unfreed thread-local storage spaces for thread idx " << id << ", not going to be re-used!" << std::endl;
			}
		}
	}
	void register_storage(detail::ThreadLocalBase::ThreadLocalPrivate* it) {
		std::lock_guard<std::mutex> lock(data_mutex);
		data.push_back(it);
	}
	void unregister_storage(detail::ThreadLocalBase::ThreadLocalPrivate* it) {
		std::lock_guard<std::mutex> lock(data_mutex);
		data.erase(std::find(data.begin(), data.end(), it));
	}
	void alloc_storage(detail::ThreadLocalBase::ThreadLocalPrivate* it, int id) {
		std::lock_guard<std::mutex> lock(mutex);
		++open[id];
		//std::cout << open[id] << " storage spaces for thread idx " << id << " (1 alloc)" << std::endl;
	}
	void free_storage(detail::ThreadLocalBase::ThreadLocalPrivate* it, int id) {
		std::lock_guard<std::mutex> lock(mutex);
		--open[id];
		//std::cout << open[id] << " storage spaces for thread idx " << id << " (1 free)" << std::endl;
	}

	static compact_thread_table& global_table() {
		static compact_thread_table thread_table;
		return thread_table;
	}
	struct thread_table_entry {
		int id = global_table().alloc_thread();
		~thread_table_entry() {
			global_table().free_thread(id);
		}
	};
	static int thread_id() {
		static thread_local thread_table_entry local_thread_entry;
		return local_thread_entry.id;
	}
};

} // namespace

/* The native TLS classes on Linux/MacOS/Windows only support a limited number
   of dynamically allocated entries (usually 1024 or 1088). Furthermore, they
   do not provide appropriate cleanup semantics when the TLS object or one of
   the assocated threads dies. The custom TLS code provided in Mitsuba has no
   such limits (caching in various subsystems of Mitsuba may create a huge amount,
   so this is a big deal) as well as nice cleanup semantics. The implementation
   is designed to make the \c get() operation as fast as as possible at the cost
   of more involved locking when creating or destroying threads and TLS objects */
namespace detail {

struct ThreadLocalBase::ThreadLocalPrivate {
	ConstructFunctor constructFunctor;
	DestructFunctor destructFunctor;

	mutable std::mutex mutex;
	std::vector<void*> tls;

	ThreadLocalPrivate(const ConstructFunctor &constructFunctor,
			const DestructFunctor &destructFunctor) : constructFunctor(constructFunctor),
			destructFunctor(destructFunctor) { compact_thread_table::global_table().register_storage(this); }

	~ThreadLocalPrivate() {
		compact_thread_table::global_table().unregister_storage(this);

		/* The TLS object was destroyed. Walk through all threads
		   and clean up where necessary */
		std::lock_guard<std::mutex> lock(mutex);

		for (int id = (int) tls.size(); id-- > 0; ) {
			void *data = tls[id];
			if (data) {
				destructFunctor(data);
				compact_thread_table::global_table().free_storage(this, id);
			}
		}
	}

	void eraseEntry(int id) {
		std::lock_guard<std::mutex> lock(mutex);
		if (id < (int) tls.size()) {
			if (void *data = tls[id]) {
				tls[id] = nullptr;
				destructFunctor(data);
				compact_thread_table::global_table().free_storage(this, id);
			}
		}
	}

	/// Look up a TLS entry. The goal is to make this operation very fast!
	std::pair<void *, bool> get() {
		bool existed = true;
		void *data;
		int id = compact_thread_table::thread_id();

		std::lock_guard<std::mutex> lock(mutex);
		if (EXPECT_NOT_TAKEN(id >= (int) tls.size())) {
			tls.resize(id + 1);
		}
		data = tls[id];
		if (EXPECT_NOT_TAKEN(!data)) {
			compact_thread_table::global_table().alloc_storage(this, id);
			/* This is the first access from this thread */
			data = constructFunctor();
			existed = false;
			tls[id] = data;
		}

		return std::make_pair(data, existed);
	}
};

ThreadLocalBase::ThreadLocalBase(
		const ConstructFunctor &constructFunctor, const DestructFunctor &destructFunctor)
		: d(new ThreadLocalPrivate(constructFunctor, destructFunctor)) { }

ThreadLocalBase::~ThreadLocalBase() { }

void *ThreadLocalBase::get() {
	return d->get().first;
}

const void *ThreadLocalBase::get() const {
	return d->get().first;
}

void *ThreadLocalBase::get(bool &existed) {
	std::pair<void *, bool> result = d->get();
	existed = result.second;
	return result.first;
}

const void *ThreadLocalBase::get(bool &existed) const {
	std::pair<void *, bool> result = d->get();
	existed = result.second;
	return result.first;
}

void initializeGlobalTLS() {
}

void destroyGlobalTLS() {
}

/// A new thread was started -- set up TLS data structures
void initializeLocalTLS() {
	// todo: currently handled by C++11 auto life time, see how that works
}

/// A thread has died -- destroy any remaining TLS entries associated with it
void destroyLocalTLS() {
	if (!has_thread_table_entry)
		return;
	compact_thread_table& table = compact_thread_table::global_table();
	int id = compact_thread_table::thread_id();

	std::lock_guard<std::mutex> lock(table.data_mutex);
	for (int i = (int) table.data.size(); i-- > 0; ) {
		auto *data = table.data[i];
		data->eraseEntry(id);
	}
}

} /* namespace detail */


MTS_NAMESPACE_END
