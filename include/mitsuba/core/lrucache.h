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

#if !defined(__LRUCACHE_H)
#define __LRUCACHE_H

#include <mitsuba/mitsuba.h>

/******************************************************************************/
/*  Copyright (c) 2010-2011, Tim Day <timday@timday.com>                      */
/*                                                                            */
/*  Permission to use, copy, modify, and/or distribute this software for any  */
/*  purpose with or without fee is hereby granted, provided that the above    */
/*  copyright notice and this permission notice appear in all copies.         */
/*                                                                            */
/*  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES  */
/*  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF          */
/*  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR   */
/*  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES    */
/*  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN     */
/*  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF   */
/*  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.            */
/******************************************************************************/

#include <cassert> 
#include <list>
#include <functional> // for std::function
#include <map>

MTS_NAMESPACE_BEGIN

/**
* \brief Generic LRU cache implementation
*
* Based on the bimap implementation by Tim Day
* (http://timday.bitbucket.org/lru.html),
* STL-only adaption from
* https://github.com/reunanen/lru-timday/blob/master/lru_cache_using_std.h
*
* This cache does not support multithreading out of the box -- it
* will need to be protected using some form of locking mechanism.
*
* The original code is under the following license:
*
* <pre>
* Copyright (c) 2010, Tim Day <timday@timday.com>
* Permission to use, copy, modify, and/or distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
* </pre>
*
* \tparam K Key data type
* \tparam KComp Key comparator
* \tparam V Value data type
* \ingroup libcore
*/
template <typename K, typename KComp, typename V> struct LRUCache : public Object {
public:

	typedef K key_type;
	typedef V value_type;

	// Key access history, most recent at back 
	typedef std::list<key_type> key_tracker_type;

	// Key to value and key history iterator 
	typedef std::map<
		key_type,
		std::pair<
			value_type,
			typename key_tracker_type::iterator
		>,
		KComp
	> key_to_value_type;

	typedef std::function<value_type(const key_type&)> function_type;
	typedef std::function<void(const value_type&)> clean_function_type;

	// Constructor specifies the cached function and 
	// the maximum number of records to be stored 
	LRUCache(
		size_t c,
		function_type f,
		clean_function_type cf = clean_function_type()
	)
		: _fn(f)
		, _cfn(cf)
		, _capacity(c)
	{
		assert(_capacity != 0);
	}
	virtual ~LRUCache() {
		if (_cfn) {
			for (typename key_to_value_type::iterator src = _key_to_value.begin(); src != _key_to_value.end(); ++src)
				_cfn(src->second.first);
		}
	}

	// Obtain value of the cached function for k 
	value_type const& get(const key_type& k, bool& hit = (bool&) bool()) {

		// Attempt to find existing record 
		typename key_to_value_type::iterator it
			= _key_to_value.find(k);

		if (it == _key_to_value.end()) {

			// We don't have it: 

			// Evaluate function and create new record 
			value_type v = _fn(k);
			it = insert(k, (value_type&&) v);

#ifdef _DEBUG
			// Update evaluation counters
			// - do it like this instead of a simple one-liner
			//   ("++_eval_counters[k]"), because now it's
			//   convenient to add a breakpoint for unexpected
			//   cache misses (counter increased beyond 1)
			const auto i = _eval_counters.find(k);
			if (i != _eval_counters.end()) {
				++i->second;
			}
			else {
				_eval_counters[k] = 1;
			}
#endif // _DEBUG

			hit = false;
		}
		else {

			// We do have it: 

			// Update access record by moving 
			// accessed key to back of list 
			_key_tracker.splice(
				_key_tracker.end(),
				_key_tracker,
				it->second.second
			);

			// Return the retrieved value 
			hit = true;
		}
		return it->second.first;
	}

	// Obtain the cached keys, most recently used element 
	// at head, least recently used at tail. 
	// This method is provided purely to support testing. 
	template <typename IT> void get_keys(IT dst) const {
		typename key_tracker_type::const_reverse_iterator src
			= _key_tracker.rbegin();
		while (src != _key_tracker.rend()) {
			*dst++ = *src++;
		}
	}

	// Using the functions has() and set(), it is possible to
	// build a thread-safe cache without having to lock the
	// whole cache in order to evaluate (and keep) a new value.

	// Find out if the cache already has some value
	bool has(const key_type& k) const {
		return _key_to_value.find(k) != _key_to_value.end();
	}

	// Set a key-value pair that may be missing in the cache
	void set(const key_type& k, const value_type& v) {
		const auto i = _key_to_value.find(k);
		if (i == _key_to_value.end()) {
			insert(k, v);
		}
		else {
			// If we already have a value, it would be logical
			// to assume that it is equal to whatever we tried
			// to set

			//assert(i->second == v);

			// However, the above assertion has been commented
			// out for now, because we don't want to require
			// that the value type has an equality operator.
			// TODO: Use SFINAE to enable the assertion when
			// the value type does have an equality operator.
		}
	}

private:

	// Record a fresh key-value pair in the cache 
	template <class KF, class VF>
	typename key_to_value_type::iterator insert(KF&& k, VF&& v) {

		// Method is only called on cache misses 
		assert(_key_to_value.find(k) == _key_to_value.end());

		// Make space if necessary 
		if (_key_to_value.size() == _capacity)
			evict();

		// Record k as most-recently-used key 
		typename key_tracker_type::iterator it
			= _key_tracker.insert(_key_tracker.end(), k);

		// Create the key-value entry, 
		// linked to the usage record. 
		return _key_to_value.insert(
			std::make_pair(
				(KF&&) k,
				std::make_pair((VF&&) v, it)
			)
		).first;
		// No need to check return, 
		// given previous assert. 
	}

	// Purge the least-recently-used element in the cache 
	void evict() {

		// Assert method is never called when cache is empty 
		assert(!_key_tracker.empty());

		// Identify least recently used key 
		const typename key_to_value_type::iterator it
			= _key_to_value.find(_key_tracker.front());
		assert(it != _key_to_value.end());

		if (_cfn) {
			_cfn(it->second.first);
		}

		// Erase both elements to completely purge record 
		_key_to_value.erase(it);
		_key_tracker.pop_front();
	}

	// The function to be cached 
	const function_type _fn;

	// The cleanup function
	const clean_function_type _cfn;

	// Maximum number of key-value pairs to be retained 
	const size_t _capacity;

	// Key access history 
	key_tracker_type _key_tracker;

	// Key-to-value lookup 
	key_to_value_type _key_to_value;

#ifdef _DEBUG
	// Evaluation counters
	std::map<key_type, size_t, KComp> _eval_counters;
#endif // _DEBUG
};

MTS_NAMESPACE_END

#endif /* __LRUCACHE_H */
