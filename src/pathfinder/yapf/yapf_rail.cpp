/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_rail.cpp The rail pathfinding. */

#include "../../stdafx.h"

#include "yapf.hpp"
#include "yapf_cache.h"
#include "yapf_node_rail.hpp"
#include "yapf_costrail.hpp"
#include "yapf_destrail.hpp"
#include "../../viewport_func.h"
#include "../../newgrf_station.h"
#include "../../tracerestrict.h"
#include "../../debug.h"
#include "../../misc/dbg_helpers.h"

#include "../../safeguards.h"

#if defined(UNIX) && defined(__GLIBC__)
#include <unistd.h>
#endif

template <typename Tpf> void DumpState(Tpf &pf1, Tpf &pf2)
{
	DumpTarget dmp1, dmp2;
	pf1.DumpBase(dmp1);
	pf2.DumpBase(dmp2);

#if defined(UNIX) && defined(__GLIBC__)
	static unsigned int num = 0;
	int pid = getpid();
	std::string fn1;
	std::string fn2;
	std::optional<FileHandle> f1;
	std::optional<FileHandle> f2;
	for(;;) {
		fn1 = fmt::format("yapf-{}-{}-1.txt", pid, num);
		f1 = FileHandle::Open(fn1, "wx");
		if (!f1.has_value() && errno == EEXIST) {
			num++;
			continue;
		}
		fn2 = fmt::format("yapf-{}-{}-2.txt", pid, num);
		f2 = FileHandle::Open(fn2, "w");
		num++;
		break;
	}
	Debug(desync, 0, "Dumping YAPF state to {} and {}", fn1, fn2);
#else
	auto f1 = FileHandle::Open("yapf1.txt", "wt");
	auto f2 = FileHandle::Open("yapf2.txt", "wt");
#endif
	assert(f1.has_value());
	assert(f2.has_value());
	fwrite(dmp1.m_out.c_str(), 1, dmp1.m_out.size(), *f1);
	fwrite(dmp2.m_out.c_str(), 1, dmp2.m_out.size(), *f2);
}

template <class Types>
class CYapfReserveTrack
{
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type

protected:
	/** to access inherited pathfinder */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

private:
	TileIndex res_dest_tile;   ///< The reservation target tile
	Trackdir res_dest_td;      ///< The reservation target trackdir
	Node *res_dest_node;       ///< The reservation target node
	TileIndex res_fail_tile;   ///< The tile where the reservation failed
	Trackdir res_fail_td;      ///< The trackdir where the reservation failed
	TileIndex origin_tile;     ///< Tile our reservation will originate from

	bool FindSafePositionProc(TileIndex tile, Trackdir td)
	{
		if (IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns())) {
			this->res_dest_tile = tile;
			this->res_dest_td = td;
			return false;   // Stop iterating segment
		}
		return true;
	}

	/** Reserve a railway platform. Tile contains the failed tile on abort. */
	bool ReserveRailStationPlatform(TileIndex &tile, DiagDirection dir)
	{
		TileIndex     start = tile;
		TileIndexDiff diff = TileOffsByDiagDir(dir);

		do {
			if (HasStationReservation(tile)) return false;
			SetRailStationReservation(tile, true);
			MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			tile = TileAdd(tile, diff);
		} while (IsCompatibleTrainStationTile(tile, start) && tile != this->origin_tile);

		TriggerStationRandomisation(nullptr, start, SRT_PATH_RESERVATION);

		return true;
	}

	/** Try to reserve a single track/platform. */
	bool ReserveSingleTrack(TileIndex tile, Trackdir td)
	{
		if (IsRailStationTile(tile)) {
			if (!ReserveRailStationPlatform(tile, TrackdirToExitdir(ReverseTrackdir(td)))) {
				/* Platform could not be reserved, undo. */
				this->res_fail_tile = tile;
				this->res_fail_td = td;
			}
		} else {
			if (!TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td)) {
				/* Tile couldn't be reserved, undo. */
				this->res_fail_tile = tile;
				this->res_fail_td = td;
				return false;
			}
		}

		return tile != this->res_dest_tile || td != this->res_dest_td;
	}

	/** Unreserve a single track/platform. Stops when the previous failure is reached. */
	bool UnreserveSingleTrack(TileIndex tile, Trackdir td)
	{
		if (IsRailStationTile(tile)) {
			TileIndex     start = tile;
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(td)));
			while ((tile != this->res_fail_tile || td != this->res_fail_td) && IsCompatibleTrainStationTile(tile, start)) {
				SetRailStationReservation(tile, false);
				tile = TileAdd(tile, diff);
			}
		} else if (tile != this->res_fail_tile || td != this->res_fail_td) {
			UnreserveRailTrackdir(tile, td);
		}
		return (tile != this->res_dest_tile || td != this->res_dest_td) && (tile != this->res_fail_tile || td != this->res_fail_td);
	}

public:
	/** Set the target to where the reservation should be extended. */
	inline void SetReservationTarget(Node *node, TileIndex tile, Trackdir td)
	{
		this->res_dest_node = node;
		this->res_dest_tile = tile;
		this->res_dest_td = td;
	}

	/** Check the node for a possible reservation target. */
	inline void FindSafePositionOnNode(Node *node)
	{
		dbg_assert(node->parent != nullptr);

		/* We will never pass more than two non-reserve-through signals, no need to check for a safe tile. */
		if (node->parent->num_signals_passed - node->parent->num_signals_res_through_passed >= 2) return;

		if (!node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::FindSafePositionProc)) {
			this->res_dest_node = node;
		}
	}

	/** Try to reserve the path till the reservation target. */
	bool TryReservePath(PBSTileInfo *target, TileIndex origin)
	{
		this->res_fail_tile = INVALID_TILE;
		this->origin_tile = origin;

		if (target != nullptr) {
			target->tile = this->res_dest_tile;
			target->trackdir = this->res_dest_td;
			target->okay = false;
		}

		/* Don't bother if the target is reserved. */
		PBSWaitingPositionRestrictedSignalState restricted_signal_state;
		restricted_signal_state.defer_test_if_slot_conditional = true;
		if (!IsWaitingPositionFree(Yapf().GetVehicle(), this->res_dest_tile, this->res_dest_td, false, &restricted_signal_state)) return false;

		/* The temporary slot state only needs to be pushed to the stack (i.e. activated) on first use */
		static TraceRestrictSlotTemporaryState temporary_slot_state;
		assert(temporary_slot_state.IsEmpty() && !temporary_slot_state.IsActive());

		struct IntermediaryTraceRestrictSignalInfo {
			const TraceRestrictProgram *prog;
			TileIndex tile;
			Trackdir trackdir;
			bool front_side;
		};
		/* Nodes are iterated in reverse order (from the target), but tiles within the node are iterated in forward order (towards the target).
		 * intermediary_restricted_signals is in reverse order, (the first signal to evaluate at the end).
		 */
		static std::vector<IntermediaryTraceRestrictSignalInfo> intermediary_restricted_signals;
		intermediary_restricted_signals.clear();

		for (Node *node = this->res_dest_node; node->parent != nullptr; node = node->parent) {
			const size_t intermediary_restricted_signals_current_size = intermediary_restricted_signals.size();
			node->template IterateTiles<CYapfReserveTrack>(Yapf().GetVehicle(), Yapf(), [&](TileIndex tile, Trackdir td) -> bool {
				/* Cheapest tests first */
				if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile) && IsRestrictedSignal(tile) && HasSignalOnTrack(tile, TrackdirToTrack(td))) {
					const bool front_side = HasSignalOnTrackdir(tile, td);

					TraceRestrictProgramActionsUsedFlags au_flags = TRPAUF_SLOT_ACQUIRE;
					if (front_side) {
						/* Passing through a signal from the front side */
						au_flags |= TRPAUF_WAIT_AT_PBS;
					}

					const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(td));
					if (prog != nullptr && prog->actions_used_flags & au_flags) {
						/* Insert at intermediary_restricted_signals_current_size, such that if there are multiple signals for this node, they end up in reverse order */
						intermediary_restricted_signals.insert(intermediary_restricted_signals.begin() + intermediary_restricted_signals_current_size, { prog, tile, td, front_side });
					}
				}

				return this->ReserveSingleTrack(tile, td);
			});
			if (this->res_fail_tile != INVALID_TILE) {
				/* Reservation failed, undo. */
				Node *fail_node = this->res_dest_node;
				TileIndex stop_tile = this->res_fail_tile;
				do {
					/* If this is the node that failed, stop at the failed tile. */
					this->res_fail_tile = fail_node == node ? stop_tile : INVALID_TILE;
					fail_node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::UnreserveSingleTrack);
				} while (fail_node != node && (fail_node = fail_node->parent) != nullptr);

				if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(Yapf().GetVehicle()->index);
				return false;
			}
		}

		auto undo_reservation = [&]() {
			for (Node *node = this->res_dest_node; node->parent != nullptr; node = node->parent) {
				node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::UnreserveSingleTrack);
			}
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(Yapf().GetVehicle()->index);
		};

		/* Iterate in reverse order */
		for (auto iter = intermediary_restricted_signals.rbegin(); iter != intermediary_restricted_signals.rend(); ++iter) {
			extern TileIndex VehiclePosTraceRestrictPreviousSignalCallback(const Train *v, const void *, TraceRestrictPBSEntrySignalAuxField mode);

			TraceRestrictProgramInput input(iter->tile, iter->trackdir, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
			if (iter->prog->actions_used_flags & TRPAUF_SLOT_ACQUIRE) {
				input.permitted_slot_operations = TRPISP_ACQUIRE_TEMP_STATE;

				if (!temporary_slot_state.IsActive()) {
					/* The temporary slot state needs to be be pushed because permission to use it is granted by TRPISP_ACQUIRE_TEMP_STATE */
					temporary_slot_state.PushToChangeStack();
				}
			}

			TraceRestrictProgramResult out;
			iter->prog->Execute(Yapf().GetVehicle(), input, out);
			if (iter->front_side && out.flags & TRPRF_WAIT_AT_PBS) {
				/* Wait at PBS is set, take this as waiting at the start signal */
				undo_reservation();
				return false;
			}
		}

		if (restricted_signal_state.deferred_test) {
			/* The IsWaitingPositionFree restricted signal test was deferred due to possible slot changes during reservation, test it now */
			if (!IsWaitingPositionFreeTraceRestrictExecute(restricted_signal_state.prog, Yapf().GetVehicle(), restricted_signal_state.tile, restricted_signal_state.trackdir)) {
				/* Target is reserved, undo reservation */
				undo_reservation();
				return false;
			}
		}

		/* This must be done before calling TraceRestrictExecuteResEndSlot */
		TraceRestrictSlotTemporaryState::ClearChangeStackApplyAllTemporaryChanges(Yapf().GetVehicle());

		restricted_signal_state.TraceRestrictExecuteResEndSlot(Yapf().GetVehicle());

		if (target != nullptr) target->okay = true;

		if (Yapf().CanUseGlobalCache(*this->res_dest_node)) {
			YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
		}

		return true;
	}

	static void stDesyncCheck(Tpf &pf1, Tpf &pf2, const char *name, bool check_res)
	{
		Node *n1 = pf1.GetBestNode();
		Node *n2 = pf2.GetBestNode();
		uint depth = 0;
		for (;;) {
			if ((n1 != nullptr) != (n2 != nullptr)) {
				Debug(desync, 0, "{}: node nonnull state at {} = [{}, {}]", name, depth, (n1 != nullptr), (n2 != nullptr));
				DumpState(pf1, pf2);
				return;
			}
			if (n1 == nullptr) break;

			if (n1->GetTile() != n2->GetTile()) {
				Debug(desync, 0, "{} tile mismatch at {} = [0x{:X}, 0x{:X}]", name, depth, n1->GetTile(), n2->GetTile());
				DumpState(pf1, pf2);
				return;
			}
			if (n1->GetTrackdir() != n2->GetTrackdir()) {
				Debug(desync, 0, "{} trackdir mismatch at {} = [0x{:X}, 0x{:X}]", name, depth, n1->GetTrackdir(), n2->GetTrackdir());
				DumpState(pf1, pf2);
				return;
			}
			n1 = n1->parent;
			n2 = n2->parent;
			depth++;
		}

		if (check_res && (pf1.res_dest_tile != pf2.res_dest_tile || pf1.res_dest_td != pf2.res_dest_td)) {
			Debug(desync, 0, "{} reservation target mismatch = [(0x{:X}, {}), (0x{:X}, {})]", name, pf1.res_dest_tile, pf1.res_dest_td, pf2.res_dest_tile, pf2.res_dest_td);
			DumpState(pf1, pf2);
			return;
		}
	}
};

template <class Types>
class CYapfFollowAnyDepotRailT
{
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** to access inherited path finder */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/**
	 * Called by YAPF to move from the given node to the next tile. For each
	 *  reachable trackdir on the new tile creates new node, initializes it
	 *  and adds it to the open list by calling Yapf().AddNewNode(n)
	 */
	inline void PfFollowNode(Node &old_node)
	{
		const Train *v = Yapf().GetVehicle();
		TrackFollower F(v);
		if (old_node.flags_u.flags_s.reverse_pending && old_node.segment->end_segment_reason.Any({EndSegmentReason::SafeTile, EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
			Node *rev_node = &old_node;
			uint length = 0;
			while (rev_node && !rev_node->segment->end_segment_reason.Test(EndSegmentReason::Reverse)) {
				length += rev_node->GetNodeLength(v, Yapf(), *this);
				rev_node = rev_node->parent;
			}
			if (rev_node && length >= v->gcache.cached_total_length) {
				if (F.Follow(rev_node->GetLastTile(), ReverseTrackdir(rev_node->GetLastTrackdir()))) {
					Yapf().AddMultipleNodes(&old_node, F, [&](Node &n) {
						n.flags_u.flags_s.reverse_pending = false;
						n.flags_u.flags_s.teleport = true;
					});
				}
				return;
			} else if (old_node.segment->end_segment_reason.Any({EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
				return;
			}
		}
		if (F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** return debug report character to identify the transportation type */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static FindDepotData stFindNearestDepotTwoWay(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int max_penalty, int reverse_penalty)
	{
		Tpf pf1;
		/*
		 * With caching enabled it simply cannot get a reliable result when you
		 * have limited the distance a train may travel. This means that the
		 * cached result does not match uncached result in all cases and that
		 * causes desyncs. So disable caching when finding for a depot that is
		 * nearby. This only happens with automatic servicing of vehicles,
		 * so it will only impact performance when you do not manually set
		 * depot orders and you do not disable automatic servicing.
		 */
		if (max_penalty != 0) pf1.DisableCache(true);
		FindDepotData result1 = pf1.FindNearestDepotTwoWay(v, t1, td1, t2, td2, max_penalty, reverse_penalty);

		if (GetDebugLevel(DebugLevelID::yapfdesync) > 0 || GetDebugLevel(DebugLevelID::desync) >= 2) {
			Tpf pf2;
			pf2.DisableCache(true);
			FindDepotData result2 = pf2.FindNearestDepotTwoWay(v, t1, td1, t2, td2, max_penalty, reverse_penalty);
			if (result1.tile != result2.tile || (result1.reverse != result2.reverse)) {
				Debug(desync, 0, "CACHE ERROR: FindNearestDepotTwoWay() = [{}, {}]",
						result1.tile != INVALID_TILE ? "T" : "F",
						result2.tile != INVALID_TILE ? "T" : "F");
				DumpState(pf1, pf2);
			}
		}

		return result1;
	}

	inline FindDepotData FindNearestDepotTwoWay(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int max_penalty, int reverse_penalty)
	{
		/* set origin and destination nodes */
		Yapf().SetOrigin(t1, td1, t2, td2, reverse_penalty);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v);
		Yapf().SetMaxCost(max_penalty);

		/* find the best path */
		if (!Yapf().FindPath(v)) return FindDepotData();

		/* Some path found. */
		Node *n = Yapf().GetBestNode();

		/* walk through the path back to the origin */
		Node *pNode = n;
		while (pNode->parent != nullptr) {
			pNode = pNode->parent;
		}

		/* if the origin node is our front vehicle tile/Trackdir then we didn't reverse
		 * but we can also look at the cost (== 0 -> not reversed, == reverse_penalty -> reversed) */
		return FindDepotData(n->GetLastTile(), n->cost, pNode->cost != 0);
	}
};

template <class Types>
class CYapfFollowAnySafeTileRailT : public CYapfReserveTrack<Types>
{
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** to access inherited path finder */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/**
	 * Called by YAPF to move from the given node to the next tile. For each
	 *  reachable trackdir on the new tile creates new node, initializes it
	 *  and adds it to the open list by calling Yapf().AddNewNode(n)
	 */
	inline void PfFollowNode(Node &old_node)
	{
		TrackFollower F(Yapf().GetVehicle(), Yapf().GetCompatibleRailTypes());
		if (F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir()) && F.MaskReservedTracks()) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** Return debug report character to identify the transportation type */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static bool stFindNearestSafeTile(const Train *v, TileIndex t1, Trackdir td, bool override_railtype)
	{
		/* Create pathfinder instance */
		Tpf pf1;
		bool result1;
		if (GetDebugLevel(DebugLevelID::yapfdesync) < 1 && GetDebugLevel(DebugLevelID::desync) < 2) {
			result1 = pf1.FindNearestSafeTile(v, t1, td, override_railtype, false);
		} else {
			bool found_path_1, found_path_2;
			pf1.FindNearestSafeTile(v, t1, td, override_railtype, true, &found_path_1);
			Tpf pf2;
			pf2.DisableCache(true);
			result1 = pf2.FindNearestSafeTile(v, t1, td, override_railtype, false, &found_path_2);
			if (found_path_1 != found_path_2) {
				Debug(desync, 0, "CACHE ERROR: FindSafeTile() = [{}, {}]", found_path_1 ? "T" : "F", found_path_2 ? "T" : "F");
				DumpState(pf1, pf2);
			} else if (found_path_2) {
				CYapfFollowAnySafeTileRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: FindSafeTile()", true);
			}
		}

		return result1;
	}

	bool FindNearestSafeTile(const Train *v, TileIndex t1, Trackdir td, bool override_railtype, bool dont_reserve, bool *found_path = nullptr)
	{
		/* Set origin and destination. */
		Yapf().SetOrigin(t1, td);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(false);
		Yapf().SetDestination(v, override_railtype);

		bool bFound = Yapf().FindPath(v);
		if (found_path) *found_path = bFound;
		if (!bFound) return false;

		/* Found a destination, set as reservation target. */
		Node *pNode = Yapf().GetBestNode();
		this->SetReservationTarget(pNode, pNode->GetLastTile(), pNode->GetLastTrackdir());

		/* Walk through the path back to the origin. */
		Node *pPrev = nullptr;
		while (pNode->parent != nullptr) {
			pPrev = pNode;
			pNode = pNode->parent;

			this->FindSafePositionOnNode(pPrev);
		}

		return dont_reserve || this->TryReservePath(nullptr, pNode->GetLastTile());
	}
};

template <class Types>
class CYapfFollowRailT : public CYapfReserveTrack<Types>
{
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** to access inherited path finder */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/**
	 * Called by YAPF to move from the given node to the next tile. For each
	 *  reachable trackdir on the new tile creates new node, initializes it
	 *  and adds it to the open list by calling Yapf().AddNewNode(n)
	 */
	inline void PfFollowNode(Node &old_node)
	{
		const Train *v = Yapf().GetVehicle();
		TrackFollower F(v);
		if (old_node.flags_u.flags_s.reverse_pending && old_node.segment->end_segment_reason.Any({EndSegmentReason::SafeTile, EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
			Node *rev_node = &old_node;
			uint length = 0;
			while (rev_node != nullptr && !rev_node->segment->end_segment_reason.Test(EndSegmentReason::Reverse)) {
				length += rev_node->GetNodeLength(v, Yapf(), *this);
				rev_node = rev_node->parent;
			}
			if (rev_node != nullptr && length >= v->gcache.cached_total_length) {
				if (F.Follow(rev_node->GetLastTile(), ReverseTrackdir(rev_node->GetLastTrackdir()))) {
					Yapf().AddMultipleNodes(&old_node, F, [&](Node &n) {
						n.flags_u.flags_s.reverse_pending = false;
						n.flags_u.flags_s.teleport = true;
					});
				}
				return;
			} else if (old_node.segment->end_segment_reason.Any({EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
				return;
			}
		}
		if (F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** return debug report character to identify the transportation type */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static Trackdir stChooseRailTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
	{
		/* create pathfinder instance */
		Tpf pf1;
		Trackdir result1;

		if (GetDebugLevel(DebugLevelID::yapfdesync) < 1 && GetDebugLevel(DebugLevelID::desync) < 2) {
			result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);
		} else {
			result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_found, false, nullptr, nullptr);
			Tpf pf2;
			pf2.DisableCache(true);
			Trackdir result2 = pf2.ChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);
			if (result1 != result2) {
				Debug(desync, 0, "CACHE ERROR: ChooseRailTrack() = [{}, {}]", result1, result2);
				DumpState(pf1, pf2);
			} else if (result1 != INVALID_TRACKDIR) {
				CYapfFollowRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: ChooseRailTrack()", true);
			}
		}

		return result1;
	}

	inline Trackdir ChooseRailTrack(const Train *v, TileIndex, DiagDirection, TrackBits, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
	{
		if (target != nullptr) target->tile = INVALID_TILE;
		if (dest != nullptr) *dest = INVALID_TILE;

		/* set origin and destination nodes */
		PBSTileInfo origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
		Yapf().SetOrigin(origin.tile, origin.trackdir, INVALID_TILE, INVALID_TRACKDIR, 1);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v);

		/* find the best path */
		path_found = Yapf().FindPath(v);

		/* if path not found - return INVALID_TRACKDIR */
		Trackdir next_trackdir = INVALID_TRACKDIR;
		Node *pNode = Yapf().GetBestNode();
		if (pNode != nullptr) {
			/* reserve till end of path */
			this->SetReservationTarget(pNode, pNode->GetLastTile(), pNode->GetLastTrackdir());

			/* path was found or at least suggested
			 * walk through the path back to the origin */
			Node *pPrev = nullptr;
			while (pNode->parent != nullptr) {
				pPrev = pNode;
				pNode = pNode->parent;

				this->FindSafePositionOnNode(pPrev);
			}

			/* If the best PF node has no parent, then there is no (valid) best next trackdir to return.
			 * This occurs when the PF is called while the train is already at its destination. */
			if (pPrev == nullptr) return INVALID_TRACKDIR;

			/* return trackdir from the best origin node (one of start nodes) */
			Node &best_next_node = *pPrev;
			next_trackdir = best_next_node.GetTrackdir();

			if (reserve_track && path_found) {
				if (dest != nullptr) *dest = Yapf().GetBestNode()->GetLastTile();
				this->TryReservePath(target, pNode->GetLastTile());
			}
		}

		/* Treat the path as found if stopped on the first two way signal(s). */
		path_found |= Yapf().stopped_on_first_two_way_signal;
		return next_trackdir;
	}

	static bool stCheckReverseTrain(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int reverse_penalty)
	{
		Tpf pf1;
		bool result1 = pf1.CheckReverseTrain(v, t1, td1, t2, td2, reverse_penalty);

		if (GetDebugLevel(DebugLevelID::yapfdesync) > 0 || GetDebugLevel(DebugLevelID::desync) >= 2) {
			Tpf pf2;
			pf2.DisableCache(true);
			bool result2 = pf2.CheckReverseTrain(v, t1, td1, t2, td2, reverse_penalty);
			if (result1 != result2) {
				Debug(desync, 2, "CACHE ERROR: CheckReverseTrain() = [{}, {}]", result1 ? "T" : "F", result2 ? "T" : "F");
				DumpState(pf1, pf2);
			} else if (result1) {
				CYapfFollowRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: CheckReverseTrain()", false);
			}
		}

		return result1;
	}

	inline bool CheckReverseTrain(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int reverse_penalty)
	{
		/* create pathfinder instance
		 * set origin and destination nodes */
		Yapf().SetOrigin(t1, td1, t2, td2, reverse_penalty);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(false);
		Yapf().SetDestination(v);

		/* find the best path */
		if (!Yapf().FindPath(v)) return false;

		/* path was found
		 * walk through the path back to the origin */
		Node *pNode = Yapf().GetBestNode();
		while (pNode->parent != nullptr) {
			pNode = pNode->parent;
		}

		/* check if it was reversed origin */
		bool reversed = (pNode->cost != 0);
		return reversed;
	}
};

template <class Tpf_, class Ttrack_follower, class Tnode_list, template <class Types> class TdestinationT, template <class Types> class TfollowT>
struct CYapfRail_TypesT
{
	typedef CYapfRail_TypesT<Tpf_, Ttrack_follower, Tnode_list, TdestinationT, TfollowT>  Types;

	typedef Tpf_                                Tpf;
	typedef Ttrack_follower                     TrackFollower;
	typedef Tnode_list                          NodeList;
	typedef Train                               VehicleType;
	typedef CYapfBaseT<Types>                   PfBase;
	typedef TfollowT<Types>                     PfFollow;
	typedef CYapfOriginTileTwoWayT<Types>       PfOrigin;
	typedef TdestinationT<Types>                PfDestination;
	typedef CYapfSegmentCostCacheGlobalT<Types> PfCache;
	typedef CYapfCostRailT<Types>               PfCost;
};

struct CYapfRail1         : CYapfT<CYapfRail_TypesT<CYapfRail1        , CFollowTrackRail    , CRailNodeListTrackDir, CYapfDestinationTileOrStationRailT, CYapfFollowRailT> > {};
struct CYapfRail2         : CYapfT<CYapfRail_TypesT<CYapfRail2        , CFollowTrackRailNo90, CRailNodeListTrackDir, CYapfDestinationTileOrStationRailT, CYapfFollowRailT> > {};

struct CYapfAnyDepotRail1 : CYapfT<CYapfRail_TypesT<CYapfAnyDepotRail1, CFollowTrackRail    , CRailNodeListTrackDir, CYapfDestinationAnyDepotRailT     , CYapfFollowAnyDepotRailT> > {};
struct CYapfAnyDepotRail2 : CYapfT<CYapfRail_TypesT<CYapfAnyDepotRail2, CFollowTrackRailNo90, CRailNodeListTrackDir, CYapfDestinationAnyDepotRailT     , CYapfFollowAnyDepotRailT> > {};

struct CYapfAnySafeTileRail1 : CYapfT<CYapfRail_TypesT<CYapfAnySafeTileRail1, CFollowTrackFreeRail    , CRailNodeListTrackDir, CYapfDestinationAnySafeTileRailT , CYapfFollowAnySafeTileRailT> > {};
struct CYapfAnySafeTileRail2 : CYapfT<CYapfRail_TypesT<CYapfAnySafeTileRail2, CFollowTrackFreeRailNo90, CRailNodeListTrackDir, CYapfDestinationAnySafeTileRailT , CYapfFollowAnySafeTileRailT> > {};


Track YapfTrainChooseTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
{
	Trackdir td_ret = _settings_game.pf.forbid_90_deg
		? CYapfRail2::stChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest)
		: CYapfRail1::stChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);

	return (td_ret != INVALID_TRACKDIR) ? TrackdirToTrack(td_ret) : FindFirstTrack(tracks);
}

bool YapfTrainCheckReverse(const Train *v)
{
	const Train *last_veh = v->Last();

	/* get trackdirs of both ends */
	Trackdir td = v->GetVehicleTrackdir();
	Trackdir td_rev = ReverseTrackdir(last_veh->GetVehicleTrackdir());

	/* tiles where front and back are */
	TileIndex tile = v->tile;
	TileIndex tile_rev = last_veh->tile;

	int reverse_penalty = 0;

	if (v->track & TRACK_BIT_WORMHOLE) {
		/* front in tunnel / on bridge */
		DiagDirection dir_into_wormhole = GetTunnelBridgeDirection(tile);

		/* Current position of the train in the wormhole */
		TileIndex cur_tile = TileVirtXY(v->x_pos, v->y_pos);

		/* Add distance to drive in the wormhole as penalty for the forward path, i.e. bonus for the reverse path
		 * Note: Negative penalties are ok for the start tile. */
		if (TrackdirToExitdir(td) == dir_into_wormhole) {
			reverse_penalty += DistanceManhattan(cur_tile, tile) * YAPF_TILE_LENGTH;
		} else {
			reverse_penalty -= DistanceManhattan(cur_tile, tile) * YAPF_TILE_LENGTH;
		}
	}

	if (last_veh->track & TRACK_BIT_WORMHOLE) {
		/* back in tunnel / on bridge */
		DiagDirection dir_into_wormhole = GetTunnelBridgeDirection(tile_rev);

		/* Current position of the last wagon in the wormhole */
		TileIndex cur_tile = TileVirtXY(last_veh->x_pos, last_veh->y_pos);

		/* Add distance to drive in the wormhole as penalty for the revere path. */
		if (TrackdirToExitdir(td_rev) == dir_into_wormhole) {
			reverse_penalty -= DistanceManhattan(cur_tile, tile_rev) * YAPF_TILE_LENGTH;
		} else {
			reverse_penalty += DistanceManhattan(cur_tile, tile_rev) * YAPF_TILE_LENGTH;
		}
	}

	/* slightly hackish: If the pathfinders finds a path, the cost of the first node is tested to distinguish between forward- and reverse-path. */
	if (reverse_penalty == 0) reverse_penalty = 1;

	bool reverse = _settings_game.pf.forbid_90_deg
		? CYapfRail2::stCheckReverseTrain(v, tile, td, tile_rev, td_rev, reverse_penalty)
		: CYapfRail1::stCheckReverseTrain(v, tile, td, tile_rev, td_rev, reverse_penalty);

	return reverse;
}

bool YapfTrainCheckDepotReverse(const Train *v, TileIndex forward_depot, TileIndex reverse_depot)
{
	typedef bool (*PfnCheckReverseTrain)(const Train*, TileIndex, Trackdir, TileIndex, Trackdir, int);
	PfnCheckReverseTrain pfnCheckReverseTrain = CYapfRail1::stCheckReverseTrain;

	/* check if non-default YAPF type needed */
	if (_settings_game.pf.forbid_90_deg) {
		pfnCheckReverseTrain = &CYapfRail2::stCheckReverseTrain; // Trackdir, forbid 90-deg
	}

	bool reverse = pfnCheckReverseTrain(v, forward_depot, DiagDirToDiagTrackdir(GetRailDepotDirection(forward_depot)),
			reverse_depot, DiagDirToDiagTrackdir(GetRailDepotDirection(reverse_depot)), 1);

	return reverse;
}

FindDepotData YapfTrainFindNearestDepot(const Train *v, int max_penalty)
{
	const Train *last_veh = v->Last();

	PBSTileInfo origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
	TileIndex last_tile = last_veh->tile;
	Trackdir td_rev = ReverseTrackdir(last_veh->GetVehicleTrackdir());

	return _settings_game.pf.forbid_90_deg
		? CYapfAnyDepotRail2::stFindNearestDepotTwoWay(v, origin.tile, origin.trackdir, last_tile, td_rev, max_penalty, YAPF_INFINITE_PENALTY)
		: CYapfAnyDepotRail1::stFindNearestDepotTwoWay(v, origin.tile, origin.trackdir, last_tile, td_rev, max_penalty, YAPF_INFINITE_PENALTY);
}

bool YapfTrainFindNearestSafeTile(const Train *v, TileIndex tile, Trackdir td, bool override_railtype)
{
	return _settings_game.pf.forbid_90_deg
		? CYapfAnySafeTileRail2::stFindNearestSafeTile(v, tile, td, override_railtype)
		: CYapfAnySafeTileRail1::stFindNearestSafeTile(v, tile, td, override_railtype);
}

/** if any track changes, this counter is incremented - that will invalidate segment cost cache */
int CSegmentCostCacheBase::s_rail_change_counter = 0;

void YapfNotifyTrackLayoutChange(TileIndex tile, Track track)
{
	CSegmentCostCacheBase::NotifyTrackLayoutChange(tile, track);
}

void YapfCheckRailSignalPenalties()
{
	bool negative = false;
	int p0 = _settings_game.pf.yapf.rail_look_ahead_signal_p0;
	int p1 = _settings_game.pf.yapf.rail_look_ahead_signal_p1;
	int p2 = _settings_game.pf.yapf.rail_look_ahead_signal_p2;
	for (int i = 0; i < (int) _settings_game.pf.yapf.rail_look_ahead_max_signals; i++) {
		if (p0 + i * (p1 + i * p2) < 0) negative = true;
	}
	if (negative) {
		Debug(misc, 0, "Settings: pf.yapf.rail_look_ahead_signal_p0, pf.yapf.rail_look_ahead_signal_p1, pf.yapf.rail_look_ahead_signal_p2 and pf.yapf.rail_look_ahead_max_signal "
				"are set to incorrect values (i.e. resulting in negative penalties), negative penalties will be truncated");
	}
}
