/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file aystar.cpp Implementation of A*.
 *
 * This file has the core function for %AyStar.
 * %AyStar is a fast path finding routine and is used for things like AI path finding and Train path finding.
 * For more information about %AyStar (A* Algorithm), you can look at
 * <A HREF='http://en.wikipedia.org/wiki/A-star_search_algorithm'>http://en.wikipedia.org/wiki/A-star_search_algorithm</A>.
 */

/*
 * Friendly reminder:
 *  Call (AyStar).free() when you are done with Aystar. It reserves a lot of memory
 *  And when not free'd, it can cause system-crashes.
 * Also remember that when you stop an algorithm before it is finished, your
 * should call clear() yourself!
 */

#include "../../stdafx.h"
#include "../../core/alloc_func.hpp"
#include "aystar.h"

#include "../../safeguards.h"
#include "core/mem_func.hpp"

/**
 * This looks in the hash whether a node exists in the closed list.
 * @param node Node to search.
 * @return The #PathNode if it is available, else \c nullptr
 */
PathNode *AyStar::ClosedListIsInList(const AyStarNode *node)
{
	const auto result = this->closedlist_hash.find(this->HashKey(node->tile, node->direction));

	return (result == this->closedlist_hash.end()) ? nullptr : this->closedlist_nodes[result->second];
}

/**
 * This adds a node to the closed list.
 * It makes a copy of the data.
 * @param node Node to add to the closed list.
 */
void AyStar::ClosedListAdd(const PathNode *node)
{
	/* Add a node to the ClosedList */
	std::pair<uint32_t, PathNode *> new_node = this->closedlist_nodes.Allocate();
	*(new_node.second) = *node;

	this->closedlist_hash[this->HashKey(node->node.tile, node->node.direction)] = new_node.first;
}

/**
 * Check whether a node is in the open list.
 * @param node Node to search.
 * @return If the node is available, it is returned, else \c UINT32_MAX is returned.
 */
uint32_t AyStar::OpenListIsInList(const AyStarNode *node)
{
	const auto result = this->openlist_hash.find(this->HashKey(node->tile, node->direction));

	return (result == this->openlist_hash.end()) ? UINT32_MAX : result->second;
}

/**
 * Gets the best node from the open list.
 * It deletes the returned node from the open list.
 * @returns the best node available, or \c nullptr of none is found.
 */
std::pair<uint32_t, OpenListNode *> AyStar::OpenListPop()
{
	/* Return the item the Queue returns.. the best next OpenList item. */
	uint32_t idx = this->openlist_queue.Pop();
	if (idx == UINT32_MAX) return std::pair<uint32_t, OpenListNode *>(idx, nullptr);

	OpenListNode *res = this->openlist_nodes[idx];
	this->openlist_hash.erase(this->HashKey(res->path.node.tile, res->path.node.direction));

	return std::make_pair(idx, res);
}

/**
 * Adds a node to the open list.
 * It makes a copy of node, and puts the pointer of parent in the struct.
 */
void AyStar::OpenListAdd(PathNode *parent, const AyStarNode *node, int f, int g)
{
	/* Add a new Node to the OpenList */
	uint32_t idx;
	OpenListNode *new_node;
	std::tie(idx, new_node) = this->openlist_nodes.Allocate();
	new_node->g = g;
	new_node->path.parent = parent;
	new_node->path.node = *node;
	this->openlist_hash[this->HashKey(node->tile, node->direction)] = idx;

	/* Add it to the queue */
	this->openlist_queue.Push(idx, f);
}

/**
 * Checks one tile and calculate its f-value
 */
void AyStar::CheckTile(AyStarNode *current, OpenListNode *parent)
{
	int new_f, new_g, new_h;
	PathNode *closedlist_parent;

	/* Check the new node against the ClosedList */
	if (this->ClosedListIsInList(current) != nullptr) return;

	/* Calculate the G-value for this node */
	new_g = this->CalculateG(this, current, parent);
	/* If the value was INVALID_NODE, we don't do anything with this node */
	if (new_g == AYSTAR_INVALID_NODE) return;

	/* There should not be given any other error-code.. */
	assert(new_g >= 0);
	/* Add the parent g-value to the new g-value */
	new_g += parent->g;
	if (this->max_path_cost != 0 && (uint)new_g > this->max_path_cost) return;

	/* Calculate the h-value */
	new_h = this->CalculateH(this, current, parent);
	/* There should not be given any error-code.. */
	assert(new_h >= 0);

	/* The f-value if g + h */
	new_f = new_g + new_h;

	/* Get the pointer to the parent in the ClosedList (the current one is to a copy of the one in the OpenList) */
	closedlist_parent = this->ClosedListIsInList(&parent->path.node);

	/* Check if this item is already in the OpenList */
	uint32_t check_idx = this->OpenListIsInList(current);
	if (check_idx != UINT32_MAX) {
		OpenListNode *check = this->openlist_nodes[check_idx];

		/* Yes, check if this g value is lower.. */
		if (new_g >= check->g) return;
		this->openlist_queue.Delete(check_idx, 0);

		/* It is lower, so change it to this item */
		check->g = new_g;
		check->path.parent = closedlist_parent;
		/* Copy user data, will probably have changed */
		for (uint i = 0; i < lengthof(current->user_data); i++) {
			check->path.node.user_data[i] = current->user_data[i];
		}
		/* Re-add it in the openlist_queue. */
		this->openlist_queue.Push(check_idx, new_f);
	} else {
		/* A new node, add it to the OpenList */
		this->OpenListAdd(closedlist_parent, current, new_f, new_g);
	}
}

/**
 * This function is the core of %AyStar. It handles one item and checks
 * its neighbour items. If they are valid, they are added to be checked too.
 * @return Possible values:
 *  - #AYSTAR_EMPTY_OPENLIST : indicates all items are tested, and no path has been found.
 *  - #AYSTAR_LIMIT_REACHED : Indicates that the max_search_nodes limit has been reached.
 *  - #AYSTAR_FOUND_END_NODE : indicates we found the end. Path_found now is true, and in path is the path found.
 *  - #AYSTAR_STILL_BUSY : indicates we have done this tile, did not found the path yet, and have items left to try.
 */
int AyStar::Loop()
{
	int i;

	/* Get the best node from OpenList */
	OpenListNode *current;
	uint32_t current_idx;
	std::tie(current_idx, current) = this->OpenListPop();
	/* If empty, drop an error */
	if (current == nullptr) return AYSTAR_EMPTY_OPENLIST;

	/* Check for end node and if found, return that code */
	if (this->EndNodeCheck(this, current) == AYSTAR_FOUND_END_NODE && (&current->path)->parent != nullptr) {
		if (this->FoundEndNode != nullptr) {
			this->FoundEndNode(this, current);
		}
		this->openlist_nodes.Free(current_idx, current);
		return AYSTAR_FOUND_END_NODE;
	}

	/* Add the node to the ClosedList */
	this->ClosedListAdd(&current->path);

	/* Load the neighbours */
	this->GetNeighbours(this, current);

	/* Go through all neighbours */
	for (i = 0; i < this->num_neighbours; i++) {
		/* Check and add them to the OpenList if needed */
		this->CheckTile(&this->neighbours[i], current);
	}

	/* Free the node */
	this->openlist_nodes.Free(current_idx, current);

	if (this->max_search_nodes != 0 && this->closedlist_hash.size() >= this->max_search_nodes) {
		/* We've expanded enough nodes */
		return AYSTAR_LIMIT_REACHED;
	} else {
		/* Return that we are still busy */
		return AYSTAR_STILL_BUSY;
	}
}

/**
 * This function frees the memory it allocated
 */
void AyStar::Free()
{
	this->openlist_queue.Free();
	this->openlist_nodes.Clear();
	this->openlist_hash.clear();
	this->closedlist_nodes.Clear();
	this->closedlist_hash.clear();
#ifdef AYSTAR_DEBUG
	printf("[AyStar] Memory free'd\n");
#endif
}

/**
 * This function make the memory go back to zero.
 * This function should be called when you are using the same instance again.
 */
void AyStar::Clear()
{
	/* Clean the Queue. */
	this->openlist_queue.Clear();

	/* Clean the hashes */
	this->openlist_nodes.Clear();
	this->openlist_hash.clear();

	this->closedlist_nodes.Clear();
	this->closedlist_hash.clear();

#ifdef AYSTAR_DEBUG
	printf("[AyStar] Cleared AyStar\n");
#endif
}

/**
 * This is the function you call to run AyStar.
 * @return Possible values:
 *  - #AYSTAR_FOUND_END_NODE : indicates we found an end node.
 *  - #AYSTAR_NO_PATH : indicates that there was no path found.
 *  - #AYSTAR_STILL_BUSY : indicates we have done some checked, that we did not found the path yet, and that we still have items left to try.
 * @note When the algorithm is done (when the return value is not #AYSTAR_STILL_BUSY) #Clear() is called automatically.
 *       When you stop the algorithm halfway, you should call #Clear() yourself!
 */
int AyStar::Main()
{
	int r, i = 0;
	/* Loop through the OpenList
	 *  Quit if result is no AYSTAR_STILL_BUSY or is more than loops_per_tick */
	while ((r = this->Loop()) == AYSTAR_STILL_BUSY && (this->loops_per_tick == 0 || ++i < this->loops_per_tick)) { }
#ifdef AYSTAR_DEBUG
	switch (r) {
		case AYSTAR_FOUND_END_NODE: printf("[AyStar] Found path!\n"); break;
		case AYSTAR_EMPTY_OPENLIST: printf("[AyStar] OpenList run dry, no path found\n"); break;
		case AYSTAR_LIMIT_REACHED:  printf("[AyStar] Exceeded search_nodes, no path found\n"); break;
		default: break;
	}
#endif
	if (r != AYSTAR_STILL_BUSY) {
		/* We're done, clean up */
		this->Clear();
	}

	switch (r) {
		case AYSTAR_FOUND_END_NODE: return AYSTAR_FOUND_END_NODE;
		case AYSTAR_EMPTY_OPENLIST:
		case AYSTAR_LIMIT_REACHED:  return AYSTAR_NO_PATH;
		default:                    return AYSTAR_STILL_BUSY;
	}
}

/**
 * Adds a node from where to start an algorithm. Multiple nodes can be added
 * if wanted. You should make sure that #Clear() is called before adding nodes
 * if the #AyStar has been used before (though the normal main loop calls
 * #Clear() automatically when the algorithm finishes.
 * @param start_node Node to start with.
 * @param g the cost for starting with this node.
 */
void AyStar::AddStartNode(AyStarNode *start_node, uint g)
{
#ifdef AYSTAR_DEBUG
	printf("[AyStar] Starting A* Algorithm from node (%d, %d, %d)\n",
		TileX(start_node->tile), TileY(start_node->tile), start_node->direction);
#endif
	this->OpenListAdd(nullptr, start_node, 0, g);
}

/**
 * Initialize an #AyStar. You should fill all appropriate fields before
 * calling #Init (see the declaration of #AyStar for which fields are internal).
 */
void AyStar::Init(uint num_buckets)
{
	MemSetT(&neighbours, 0);
	MemSetT(&openlist_queue, 0);

	/* Set up our sorting queue
	 *  BinaryHeap allocates a block of 1024 nodes
	 *  When that one gets full it reserves another one, till this number
	 *  That is why it can stay this high */
	this->openlist_queue.Init(102400);
}
