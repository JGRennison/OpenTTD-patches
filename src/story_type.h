/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file story_type.h basic types related to story pages */

#ifndef STORY_TYPE_H
#define STORY_TYPE_H

#include "core/enum_type.hpp"
#include "core/pool_id_type.hpp"

struct StoryPageElementIDTag : public PoolIDTraits<uint16_t, 64000, 0xFFFF> {};
using StoryPageElementID = PoolID<StoryPageElementIDTag>; ///< ID of a story page element
struct StoryPageIDTag : public PoolIDTraits<uint16_t, 64000, 0xFFFF> {};
using StoryPageID = PoolID<StoryPageIDTag>; ///< ID of a story page
struct StoryPageElement;
struct StoryPage;
enum StoryPageElementType : uint8_t;

#endif /* STORY_TYPE_H */

