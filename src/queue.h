/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <vector>
#include <mutex>

template<typename T>
struct Queue {
	std::mutex items_lock;
	std::vector<T> items;

	void clear(void) {
		items_lock.lock();
		items.clear();
		items_lock.unlock();
	}

	void add_item(T item) {
		items_lock.lock();
		items.push_back(item);
		items_lock.unlock();
	}

	T next_item(size_t backbuffer = 0) {
		T item{};
		if (items.size() > backbuffer) {
			items_lock.lock();
			item = items.front();
			items.erase(items.begin());
			items_lock.unlock();
		}
		return item;
	}
};

#endif