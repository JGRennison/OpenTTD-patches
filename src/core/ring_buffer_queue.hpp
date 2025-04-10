/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ring_buffer_queue.hpp std::queue backed by a ring buffer. */

#ifndef RING_BUFFER_QUEUE_HPP
#define RING_BUFFER_QUEUE_HPP

#include "ring_buffer.hpp"

#include <queue>

template <class T>
using ring_buffer_queue = std::queue<T, ring_buffer<T>>;

#endif /* RING_BUFFER_QUEUE_HPP */
