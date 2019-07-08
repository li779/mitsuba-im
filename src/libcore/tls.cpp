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

#include <mutex>

MTS_NAMESPACE_BEGIN

struct compact_thread_table {
	std::mutex mutex;
	std::vector<int> free_ids;
	std::vector<int> open;

	int alloc_thread() {
		std::lock_guard<std::mutex> lock(mutex);
		if (free_ids.empty()) {
			int next_id = (int) open.size();
			open.push_back(0);
			return next_id;
		} else {
			int next_id = free_ids.back();
			free_ids.pop_back();
			assert(open[next_id] == 0);
			return next_id;
		}
	}
	void free_thread(int id) {
		std::lock_guard<std::mutex> lock(mutex);
		if (!open[id]) {
			free_ids.push_back(id);
		} else {
			std::cout << open[id] << " unfreed thread-local storage spaces for thread idx " << id << ", not gonna be re-used!";
		}
	}
	void alloc_storage(int id) {
		std::lock_guard<std::mutex> lock(mutex);
		++open[id];
	}
	void free_storage(int id) {
		std::lock_guard<std::mutex> lock(mutex);
		--open[id];
	}
};

compact_thread_table thread_table;

struct thread_table_entry {
	int id = thread_table.alloc_thread();
	~thread_table_entry() { thread_table.free_thread(id); }
};

thread_local thread_table_entry local_thread_entry;

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
			destructFunctor(destructFunctor) { thread_table.alloc_storage(local_thread_entry.id); }

	~ThreadLocalPrivate() {
		thread_table.free_storage(local_thread_entry.id);

		/* The TLS object was destroyed. Walk through all threads
		   and clean up where necessary */
		std::lock_guard<std::mutex> lock(mutex);

		for (auto data : tls) {
			if (data)
				destructFunctor(data);
		}
	}

	/// Look up a TLS entry. The goal is to make this operation very fast!
	std::pair<void *, bool> get() {
		bool existed = true;
		void *data;
		int id = local_thread_entry.id;

		std::lock_guard<std::mutex> lock(mutex);
		if (EXPECT_NOT_TAKEN(id >= (int) tls.size())) {
			tls.resize(id + 1);
		}
		data = tls[id];
		if (EXPECT_NOT_TAKEN(!data)) {
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
	// todo: currently handled by C++11 auto life time, see how that works
}

} /* namespace detail */


MTS_NAMESPACE_END
