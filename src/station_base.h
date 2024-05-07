/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_base.h Base classes/functions for stations. */

#ifndef STATION_BASE_H
#define STATION_BASE_H

#include "core/random_func.hpp"
#include "base_station_base.h"
#include "newgrf_airport.h"
#include "cargopacket.h"
#include "industry_type.h"
#include "linkgraph/linkgraph_type.h"
#include "newgrf_storage.h"
#include "3rdparty/cpp-btree/btree_map.h"
#include "3rdparty/cpp-btree/btree_set.h"
#include "bitmap_type.h"
#include "core/alloc_type.hpp"
#include "core/endian_type.hpp"
#include "strings_type.h"
#include <map>
#include <vector>
#include <array>
#include <iterator>
#include <functional>
#include <algorithm>

static const uint8_t INITIAL_STATION_RATING = 175;
static const uint8_t MAX_STATION_RATING = 255;

static const uint MAX_EXTRA_STATION_NAMES = 1024;

/** Extra station name string flags. */
enum ExtraStationNameInfoFlags {
	/* Bits 0 - 5 used for StationNaming enum */
	ESNIF_CENTRAL               =  8,
	ESNIF_NOT_CENTRAL           =  9,
	ESNIF_NEAR_WATER            = 10,
	ESNIF_NOT_NEAR_WATER        = 11,
};

/** Extra station name string */
struct ExtraStationNameInfo {
	StringID str;
	uint16_t flags;
};

extern std::array<ExtraStationNameInfo, MAX_EXTRA_STATION_NAMES> _extra_station_names;
extern uint _extra_station_names_used;
extern uint8_t _extra_station_names_probability;

class FlowStatMap;

extern const StationCargoList _empty_cargo_list;
extern const FlowStatMap _empty_flows;

/**
 * Flow statistics telling how much flow should be sent along a link. This is
 * done by creating "flow shares" and using std::map's upper_bound() method to
 * look them up with a random number. A flow share is the difference between a
 * key in a map and the previous key. So one key in the map doesn't actually
 * mean anything by itself.
 */
class FlowStat {
	friend FlowStatMap;
public:
	struct ShareEntry {
#if OTTD_ALIGNMENT == 0
		unaligned_uint32 first;
#else
		uint32_t first;
#endif
		StationID second;
	};
#if OTTD_ALIGNMENT == 0 && (defined(__GNUC__) || defined(__clang__))
	static_assert(sizeof(ShareEntry) == 6, "");
#endif

	friend bool operator<(const ShareEntry &a, const ShareEntry &b) noexcept
	{
		return a.first < b.first;
	}

	friend bool operator<(uint a, const ShareEntry &b) noexcept
	{
		return a < b.first;
	}

	typedef ShareEntry* iterator;
	typedef const ShareEntry* const_iterator;

	/**
	 * Invalid constructor. This can't be called as a FlowStat must not be
	 * empty. However, the constructor must be defined and reachable for
	 * FlowStat to be used in a std::map.
	 */
	inline FlowStat() {NOT_REACHED();}

	/**
	 * Create a FlowStat with an initial entry.
	 * @param origin Origin station for this flow.
	 * @param via Station the initial entry refers to.
	 * @param flow Amount of flow for the initial entry.
	 * @param restricted If the flow to be added is restricted.
	 */
	inline FlowStat(StationID origin, StationID via, uint flow, bool restricted = false)
	{
		assert(flow > 0);
		this->storage.inline_shares[0].first = flow;
		this->storage.inline_shares[0].second = via;
		this->unrestricted = restricted ? 0 : flow;
		this->count = 1;
		this->origin = origin;
		this->flags = 0;
	}

private:
	inline bool inline_mode() const
	{
		return this->count <= 2;
	}

	inline const ShareEntry *data() const
	{
		return this->inline_mode() ? this->storage.inline_shares : this->storage.ptr_shares.buffer;
	}

	inline ShareEntry *data()
	{
		return const_cast<ShareEntry *>(const_cast<const FlowStat*>(this)->data());
	}

	inline void clear()
	{
		if (!inline_mode()) {
			free(this->storage.ptr_shares.buffer);
		}
		this->count = 0;
		this->flags = 0;
	}

	iterator erase_item(iterator iter, uint flow_reduction);

	inline void CopyCommon(const FlowStat &other)
	{
		this->count = other.count;
		if (!other.inline_mode()) {
			this->storage.ptr_shares.elem_capacity = other.storage.ptr_shares.elem_capacity;
			this->storage.ptr_shares.buffer = MallocT<ShareEntry>(other.storage.ptr_shares.elem_capacity);
		}
		MemCpyT(this->data(), other.data(), this->count);
		this->unrestricted = other.unrestricted;
		this->origin = other.origin;
		this->flags = other.flags;
	}

public:
	inline FlowStat(const FlowStat &other)
	{
		this->CopyCommon(other);
	}

	inline FlowStat(FlowStat &&other) noexcept
	{
		this->count = 0;
		this->SwapShares(other);
		this->origin = other.origin;
	}

	inline ~FlowStat()
	{
		this->clear();
	}

	inline FlowStat &operator=(const FlowStat &other)
	{
		this->clear();
		this->CopyCommon(other);
		return *this;
	}

	inline FlowStat &operator=(FlowStat &&other) noexcept
	{
		this->SwapShares(other);
		this->origin = other.origin;
		return *this;
	}

	inline size_t size() const { return this->count; }
	inline bool empty() const { return this->count == 0; }
	inline iterator begin() { return this->data(); }
	inline const_iterator begin() const { return this->data(); }
	inline iterator end() { return this->data() + this->count; }
	inline const_iterator end() const { return this->data() + this->count; }
	inline iterator upper_bound(uint32_t key) { return std::upper_bound(this->begin(), this->end(), key); }
	inline const_iterator upper_bound(uint32_t key) const { return std::upper_bound(this->begin(), this->end(), key); }

	/**
	 * Add some flow to the end of the shares map. Only do that if you know
	 * that the station isn't in the map yet. Anything else may lead to
	 * inconsistencies.
	 * @param st Remote station.
	 * @param flow Amount of flow to be added.
	 * @param restricted If the flow to be added is restricted.
	 */
	inline void AppendShare(StationID st, uint flow, bool restricted = false)
	{
		assert(flow > 0);
		uint32_t key = this->GetLastKey() + flow;
		if (unlikely(this->count >= 2)) {
			if (this->count == 2) {
				// convert inline buffer to ptr
				ShareEntry *ptr = MallocT<ShareEntry>(4);
				ptr[0] = this->storage.inline_shares[0];
				ptr[1] = this->storage.inline_shares[1];
				this->storage.ptr_shares.buffer = ptr;
				this->storage.ptr_shares.elem_capacity = 4;
			} else if (this->count == this->storage.ptr_shares.elem_capacity) {
				// grow buffer
				uint16_t new_size = this->storage.ptr_shares.elem_capacity * 2;
				this->storage.ptr_shares.buffer = ReallocT<ShareEntry>(this->storage.ptr_shares.buffer, new_size);
				this->storage.ptr_shares.elem_capacity = new_size;
			}
			this->storage.ptr_shares.buffer[this->count] = { key, st };
		} else {
			this->storage.inline_shares[this->count] = { key, st };
		}
		this->count++;
		if (!restricted) this->unrestricted += flow;
	}

	uint GetShare(StationID st) const;

	void ChangeShare(StationID st, int flow);

	void RestrictShare(StationID st);

	void ReleaseShare(StationID st);

	void ScaleToMonthly(uint runtime);

	/**
	 * Return total amount of unrestricted shares.
	 * @return Amount of unrestricted shares.
	 */
	inline uint GetUnrestricted() const { return this->unrestricted; }

	/**
	 * Swap the shares maps, and thus the content of this FlowStat with the
	 * other one.
	 * @param other FlowStat to swap with.
	 */
	inline void SwapShares(FlowStat &other)
	{
		std::swap(this->storage, other.storage);
		std::swap(this->unrestricted, other.unrestricted);
		std::swap(this->count, other.count);
		std::swap(this->flags, other.flags);
	}

	/**
	 * Get a station a package can be routed to. This done by drawing a
	 * random number between 0 and sum_shares and then looking that up in
	 * the map with lower_bound. So each share gets selected with a
	 * probability dependent on its flow. Do include restricted flows here.
	 * @param is_restricted Output if a restricted flow was chosen.
	 * @return A station ID from the shares map.
	 */
	inline StationID GetViaWithRestricted(bool &is_restricted) const
	{
		assert(!this->empty());
		uint rand = RandomRange(this->GetLastKey());
		is_restricted = rand >= this->unrestricted;
		return this->upper_bound(rand)->second;
	}

	/**
	 * Get a station a package can be routed to. This done by drawing a
	 * random number between 0 and sum_shares and then looking that up in
	 * the map with lower_bound. So each share gets selected with a
	 * probability dependent on its flow. Don't include restricted flows.
	 * @return A station ID from the shares map.
	 */
	inline StationID GetVia() const
	{
		assert(!this->empty());
		return this->unrestricted > 0 ?
				this->upper_bound(RandomRange(this->unrestricted))->second :
				INVALID_STATION;
	}

	StationID GetVia(StationID excluded, StationID excluded2 = INVALID_STATION) const;

	/**
	 * Mark this flow stat as invalid, such that it is not included in link statistics.
	 * @return True if the flow stat should be deleted.
	 */
	inline bool Invalidate()
	{
		if ((this->flags & 0x1F) == 0x1F) return true;
		this->flags++;
		return false;
	}

	inline StationID GetOrigin() const
	{
		return this->origin;
	}

	inline bool IsInvalid() const
	{
		return (this->flags & 0x1F) != 0;
	}

	/* for save/load use only */
	inline uint16_t GetRawFlags() const
	{
		return this->flags;
	}

	/* for save/load use only */
	inline void SetRawFlags(uint16_t flags)
	{
		this->flags = flags;;
	}

private:
	uint32_t GetLastKey() const
	{
		return this->data()[this->count - 1].first;
	}

	struct ptr_buffer {
		ShareEntry *buffer;
		uint16_t elem_capacity;
	}
#if OTTD_ALIGNMENT == 0 && (defined(__GNUC__) || defined(__clang__))
	__attribute__((packed, aligned(4)))
#endif
	;
	union storage_union {
		ShareEntry inline_shares[2]; ///< Small buffer optimisation: size = 1 is ~90%, size = 2 is ~9%, size >= 3 is ~1%
		ptr_buffer ptr_shares;

		// Actual construction/destruction done by class FlowStat
		storage_union() {}
		~storage_union() {}
	};
	storage_union storage; ///< Shares of flow to be sent via specified station (or consumed locally).
	uint unrestricted; ///< Limit for unrestricted shares.
	uint16_t count;
	StationID origin;
	uint16_t flags;
};
static_assert(std::is_nothrow_move_constructible<FlowStat>::value, "FlowStat must be nothrow move constructible");
#if OTTD_ALIGNMENT == 0 && (defined(__GNUC__) || defined(__clang__))
static_assert(sizeof(FlowStat) == 24, "");
#endif

template<typename cv_value, typename cv_container, typename cv_index_iter>
class FlowStatMapIterator
{
	friend FlowStatMap;
	friend FlowStatMapIterator<FlowStat, FlowStatMap, btree::btree_map<StationID, uint16_t>::iterator>;
	friend FlowStatMapIterator<const FlowStat, const FlowStatMap, btree::btree_map<StationID, uint16_t>::const_iterator>;
public:
	typedef FlowStat value_type;
	typedef cv_value& reference;
	typedef cv_value* pointer;
	typedef ptrdiff_t difference_type;
	typedef std::forward_iterator_tag iterator_category;

	FlowStatMapIterator(cv_container *fsm, cv_index_iter current) :
		fsm(fsm), current(current) {}

	FlowStatMapIterator(const FlowStatMapIterator<FlowStat, FlowStatMap, btree::btree_map<StationID, uint16_t>::iterator> &other) :
		fsm(other.fsm), current(other.current) {}

	FlowStatMapIterator &operator=(const FlowStatMapIterator &) = default;

	reference operator*() const { return this->fsm->flows_storage[this->current->second]; }
	pointer operator->() const { return &(this->fsm->flows_storage[this->current->second]); }

	FlowStatMapIterator& operator++()
	{
		++this->current;
		return *this;
	}

	bool operator==(const FlowStatMapIterator& rhs) const { return this->current == rhs.current; }
	bool operator!=(const FlowStatMapIterator& rhs) const { return !(operator==(rhs)); }

private:
	cv_container *fsm;
	cv_index_iter current;
};

/** Flow descriptions by origin stations. */
class FlowStatMap {
	std::vector<FlowStat> flows_storage;
	btree::btree_map<StationID, uint16_t> flows_index;

public:
	using iterator = FlowStatMapIterator<FlowStat, FlowStatMap, btree::btree_map<StationID, uint16_t>::iterator>;
	using const_iterator = FlowStatMapIterator<const FlowStat, const FlowStatMap, btree::btree_map<StationID, uint16_t>::const_iterator>;

	friend iterator;
	friend const_iterator;

	uint GetFlow() const;
	uint GetFlowVia(StationID via) const;
	uint GetFlowFrom(StationID from) const;
	uint GetFlowFromVia(StationID from, StationID via) const;

	void AddFlow(StationID origin, StationID via, uint amount);
	void PassOnFlow(StationID origin, StationID via, uint amount);
	StationIDStack DeleteFlows(StationID via);
	void RestrictFlows(StationID via);
	void FinalizeLocalConsumption(StationID self);

private:
	btree::btree_map<StationID, uint16_t>::iterator erase_priv(btree::btree_map<StationID, uint16_t>::iterator iter)
	{
		uint16_t index = iter->second;
		iter = this->flows_index.erase(iter);
		if (index != this->flows_storage.size() - 1) {
			this->flows_storage[index] = std::move(this->flows_storage.back());
			this->flows_index[this->flows_storage[index].GetOrigin()] = index;
		}
		this->flows_storage.pop_back();
		return iter;
	}

public:
	iterator begin() { return iterator(this, this->flows_index.begin()); }
	const_iterator begin() const { return const_iterator(this, this->flows_index.begin()); }
	iterator end() { return iterator(this, this->flows_index.end()); }
	const_iterator end() const { return const_iterator(this, this->flows_index.end()); }

	iterator find(StationID from)
	{
		return iterator(this, this->flows_index.find(from));
	}
	const_iterator find(StationID from) const
	{
		return const_iterator(this, this->flows_index.find(from));
	}

	bool empty() const
	{
		return this->flows_storage.empty();
	}

	size_t size() const
	{
		return this->flows_storage.size();
	}

	void erase(StationID st)
	{
		auto iter = this->flows_index.find(st);
		if (iter != this->flows_index.end()) {
			this->erase_priv(iter);
		}
	}

	iterator erase(iterator iter)
	{
		return iterator(this, this->erase_priv(iter.current));
	}

	std::pair<iterator, bool> insert(FlowStat flow_stat)
	{
		StationID st = flow_stat.GetOrigin();
		auto res = this->flows_index.insert(std::pair<StationID, uint16_t>(st, (uint16_t)this->flows_storage.size()));
		if (res.second) {
			this->flows_storage.push_back(std::move(flow_stat));
		}
		return std::make_pair(iterator(this, res.first), res.second);
	}

	iterator insert(iterator hint, FlowStat flow_stat)
	{
		auto res = this->flows_index.insert(hint.current, std::pair<StationID, uint16_t>(flow_stat.GetOrigin(), (uint16_t)this->flows_storage.size()));
		if (res->second == this->flows_storage.size()) {
			this->flows_storage.push_back(std::move(flow_stat));
		}
		return iterator(this, res);
	}

	StationID FirstStationID() const
	{
		return this->flows_index.begin()->first;
	}

	void reserve(size_t size)
	{
		this->flows_storage.reserve(size);
	}

	void SortStorage();

	std::span<const FlowStat> IterateUnordered() const
	{
		return std::span<const FlowStat>(this->flows_storage.data(), this->flows_storage.size());
	}
};

struct GoodsEntryData : ZeroedMemoryAllocator {
	StationCargoList cargo; ///< The cargo packets of cargo waiting in this station
	FlowStatMap flows;      ///< Planned flows through this station.

	bool MayBeRemoved() const
	{
		return this->cargo.Packets()->MapSize() == 0 && this->cargo.ReservedCount() == 0 && this->flows.empty();
	}
};

/**
 * Stores station stats for a single cargo.
 */
struct GoodsEntry {
	/** Status of this cargo for the station. */
	enum GoodsEntryStatus {
		/**
		 * Set when the station accepts the cargo currently for final deliveries.
		 * It is updated every STATION_ACCEPTANCE_TICKS ticks by checking surrounding tiles for acceptance >= 8/8.
		 */
		GES_ACCEPTANCE,

		/**
		 * This indicates whether a cargo has a rating at the station.
		 * Set when cargo was ever waiting at the station.
		 * It is set when cargo supplied by surrounding tiles is moved to the station, or when
		 * arriving vehicles unload/transfer cargo without it being a final delivery.
		 *
		 * This flag is cleared after 255 * STATION_RATING_TICKS of not having seen a pickup.
		 */
		GES_RATING,

		/**
		 * Set when a vehicle ever delivered cargo to the station for final delivery.
		 * This flag is never cleared.
		 */
		GES_EVER_ACCEPTED,

		/**
		 * Set when cargo was delivered for final delivery last month.
		 * This flag is set to the value of GES_CURRENT_MONTH at the start of each month.
		 */
		GES_LAST_MONTH,

		/**
		 * Set when cargo was delivered for final delivery this month.
		 * This flag is reset on the beginning of every month.
		 */
		GES_CURRENT_MONTH,

		/**
		 * Set when cargo was delivered for final delivery during the current STATION_ACCEPTANCE_TICKS interval.
		 * This flag is reset every STATION_ACCEPTANCE_TICKS ticks.
		 */
		GES_ACCEPTED_BIGTICK,

		/**
		 * Set when cargo is not permitted to be supplied by nearby industries/houses.
		 */
		GES_NO_CARGO_SUPPLY = 7,
	};

	GoodsEntry() :
		status(0),
		time_since_pickup(255),
		last_vehicle_type(VEH_INVALID),
		rating(INITIAL_STATION_RATING),
		last_speed(0),
		last_age(255),
		amount_fract(0),
		link_graph(INVALID_LINK_GRAPH),
		node(INVALID_NODE),
		max_waiting_cargo(0)
	{}

	uint8_t status; ///< Status of this cargo, see #GoodsEntryStatus.

	/**
	 * Number of rating-intervals (up to 255) since the last vehicle tried to load this cargo.
	 * The unit used is STATION_RATING_TICKS.
	 * This does not imply there was any cargo to load.
	 */
	uint8_t time_since_pickup;

	uint8_t last_vehicle_type;

	uint8_t rating;         ///< %Station rating for this cargo.

	/**
	 * Maximum speed (up to 255) of the last vehicle that tried to load this cargo.
	 * This does not imply there was any cargo to load.
	 * The unit used is a special vehicle-specific speed unit for station ratings.
	 *  - Trains: km-ish/h
	 *  - RV: km-ish/h
	 *  - Ships: 0.5 * km-ish/h
	 *  - Aircraft: 8 * mph
	 */
	uint8_t last_speed;

	/**
	 * Age in years (up to 255) of the last vehicle that tried to load this cargo.
	 * This does not imply there was any cargo to load.
	 */
	uint8_t last_age;

	uint8_t amount_fract;   ///< Fractional part of the amount in the cargo list

	std::unique_ptr<GoodsEntryData> data;

	LinkGraphID link_graph; ///< Link graph this station belongs to.
	NodeID node;            ///< ID of node in link graph referring to this goods entry.

	uint max_waiting_cargo; ///< Max cargo from this station waiting at any station.

	bool IsSupplyAllowed() const
	{
		return !HasBit(this->status, GES_NO_CARGO_SUPPLY);
	}

	/**
	 * Reports whether a vehicle has ever tried to load the cargo at this station.
	 * This does not imply that there was cargo available for loading. Refer to GES_RATING for that.
	 * @return true if vehicle tried to load.
	 */
	bool HasVehicleEverTriedLoading() const { return this->last_speed != 0; }

	/**
	 * Does this cargo have a rating at this station?
	 * @return true if the cargo has a rating, i.e. cargo has been moved to the station.
	 */
	inline bool HasRating() const
	{
		return HasBit(this->status, GES_RATING);
	}

	/**
	 * Get the best next hop for a cargo packet from station source.
	 * @param source Source of the packet.
	 * @return The chosen next hop or INVALID_STATION if none was found.
	 */
	inline StationID GetVia(StationID source) const
	{
		if (this->data == nullptr) return INVALID_STATION;

		FlowStatMap::const_iterator flow_it(this->data->flows.find(source));
		return flow_it != this->data->flows.end() ? flow_it->GetVia() : INVALID_STATION;
	}

	/**
	 * Get the best next hop for a cargo packet from station source, optionally
	 * excluding one or two stations.
	 * @param source Source of the packet.
	 * @param excluded If this station would be chosen choose the second best one instead.
	 * @param excluded2 Second station to be excluded, if != INVALID_STATION.
	 * @return The chosen next hop or INVALID_STATION if none was found.
	 */
	inline StationID GetVia(StationID source, StationID excluded, StationID excluded2 = INVALID_STATION) const
	{
		if (this->data == nullptr) return INVALID_STATION;

		FlowStatMap::const_iterator flow_it(this->data->flows.find(source));
		return flow_it != this->data->flows.end() ? flow_it->GetVia(excluded, excluded2) : INVALID_STATION;
	}

	GoodsEntryData &CreateData()
	{
		if (this->data == nullptr) this->data.reset(new GoodsEntryData());
		return *this->data;
	}

	const GoodsEntryData &CreateData() const
	{
		if (this->data == nullptr) const_cast<GoodsEntry *>(this)->data.reset(new GoodsEntryData());
		return *this->data;
	}

	inline uint CargoAvailableCount() const
	{
		return this->data != nullptr ? this->data->cargo.AvailableCount() : 0;
	}

	inline uint CargoReservedCount() const
	{
		return this->data != nullptr ? this->data->cargo.ReservedCount() : 0;
	}

	inline uint CargoTotalCount() const
	{
		return this->data != nullptr ? this->data->cargo.TotalCount() : 0;
	}

	inline uint CargoAvailableViaCount(StationID next) const
	{
		return this->data != nullptr ? this->data->cargo.AvailableViaCount(next) : 0;
	}

	const StationCargoList &ConstCargoList() const
	{
		return this->data != nullptr ? this->data->cargo : _empty_cargo_list;
	}

	const FlowStatMap &ConstFlows() const
	{
		return this->data != nullptr ? this->data->flows : _empty_flows;
	}
};

/** All airport-related information. Only valid if tile != INVALID_TILE. */
struct Airport : public TileArea {
	Airport() : TileArea(INVALID_TILE, 0, 0) {}

	uint64_t flags;     ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32
	uint8_t type;       ///< Type of this airport, @see AirportTypes
	uint8_t layout;     ///< Airport layout number.
	Direction rotation; ///< How this airport is rotated.

	PersistentStorage *psa; ///< Persistent storage for NewGRF airports.

	/**
	 * Get the AirportSpec that from the airport type of this airport. If there
	 * is no airport (\c tile == INVALID_TILE) then return the dummy AirportSpec.
	 * @return The AirportSpec for this airport.
	 */
	const AirportSpec *GetSpec() const
	{
		if (this->tile == INVALID_TILE) return &AirportSpec::dummy;
		return AirportSpec::Get(this->type);
	}

	/**
	 * Get the finite-state machine for this airport or the finite-state machine
	 * for the dummy airport in case this isn't an airport.
	 * @pre this->type < NEW_AIRPORT_OFFSET.
	 * @return The state machine for this airport.
	 */
	const AirportFTAClass *GetFTA() const
	{
		return this->GetSpec()->fsm;
	}

	/** Check if this airport has at least one hangar. */
	inline bool HasHangar() const
	{
		return this->GetSpec()->nof_depots > 0;
	}

	/**
	 * Add the tileoffset to the base tile of this airport but rotate it first.
	 * The base tile is the northernmost tile of this airport. This function
	 * helps to make sure that getting the tile of a hangar works even for
	 * rotated airport layouts without requiring a rotated array of hangar tiles.
	 * @param tidc The tilediff to add to the airport tile.
	 * @return The tile of this airport plus the rotated offset.
	 */
	inline TileIndex GetRotatedTileFromOffset(TileIndexDiffC tidc) const
	{
		const AirportSpec *as = this->GetSpec();
		switch (this->rotation) {
			case DIR_N: return this->tile + ToTileIndexDiff(tidc);

			case DIR_E: return this->tile + TileDiffXY(tidc.y, as->size_x - 1 - tidc.x);

			case DIR_S: return this->tile + TileDiffXY(as->size_x - 1 - tidc.x, as->size_y - 1 - tidc.y);

			case DIR_W: return this->tile + TileDiffXY(as->size_y - 1 - tidc.y, tidc.x);

			default: NOT_REACHED();
		}
	}

	/**
	 * Get the first tile of the given hangar.
	 * @param hangar_num The hangar to get the location of.
	 * @pre hangar_num < GetNumHangars().
	 * @return A tile with the given hangar.
	 */
	inline TileIndex GetHangarTile(uint hangar_num) const
	{
		const AirportSpec *as = this->GetSpec();
		for (uint i = 0; i < as->nof_depots; i++) {
			if (as->depot_table[i].hangar_num == hangar_num) {
				return this->GetRotatedTileFromOffset(as->depot_table[i].ti);
			}
		}
		NOT_REACHED();
	}

	/**
	 * Get the exit direction of the hangar at a specific tile.
	 * @param tile The tile to query.
	 * @pre IsHangarTile(tile).
	 * @return The exit direction of the hangar, taking airport rotation into account.
	 */
	inline Direction GetHangarExitDirection(TileIndex tile) const
	{
		const AirportSpec *as = this->GetSpec();
		const HangarTileTable *htt = GetHangarDataByTile(tile);
		return ChangeDir(htt->dir, DirDifference(this->rotation, as->rotation[0]));
	}

	/**
	 * Get the hangar number of the hangar at a specific tile.
	 * @param tile The tile to query.
	 * @pre IsHangarTile(tile).
	 * @return The hangar number of the hangar at the given tile.
	 */
	inline uint GetHangarNum(TileIndex tile) const
	{
		const HangarTileTable *htt = GetHangarDataByTile(tile);
		return htt->hangar_num;
	}

	/** Get the number of hangars on this airport. */
	inline uint GetNumHangars() const
	{
		uint num = 0;
		uint counted = 0;
		const AirportSpec *as = this->GetSpec();
		for (uint i = 0; i < as->nof_depots; i++) {
			if (!HasBit(counted, as->depot_table[i].hangar_num)) {
				num++;
				SetBit(counted, as->depot_table[i].hangar_num);
			}
		}
		return num;
	}

private:
	/**
	 * Retrieve hangar information of a hangar at a given tile.
	 * @param tile %Tile containing the hangar.
	 * @return The requested hangar information.
	 * @pre The \a tile must be at a hangar tile at an airport.
	 */
	inline const HangarTileTable *GetHangarDataByTile(TileIndex tile) const
	{
		const AirportSpec *as = this->GetSpec();
		for (uint i = 0; i < as->nof_depots; i++) {
			if (this->GetRotatedTileFromOffset(as->depot_table[i].ti) == tile) {
				return as->depot_table + i;
			}
		}
		NOT_REACHED();
	}
};

struct IndustryListEntry {
	uint distance;
	Industry *industry;

	bool operator==(const IndustryListEntry &other) const { return this->distance == other.distance && this->industry == other.industry; }
	bool operator!=(const IndustryListEntry &other) const { return !(*this == other); }
};

struct IndustryCompare {
	bool operator() (const IndustryListEntry &lhs, const IndustryListEntry &rhs) const;
};

typedef btree::btree_set<IndustryListEntry, IndustryCompare> IndustryList;

/** Station data structure */
struct Station final : SpecializedStation<Station, false> {
public:
	RoadStop *GetPrimaryRoadStop(RoadStopType type) const
	{
		return type == ROADSTOP_BUS ? bus_stops : truck_stops;
	}

	RoadStop *GetPrimaryRoadStop(const struct RoadVehicle *v) const;

	RoadStop *bus_stops;    ///< All the road stops
	TileArea bus_station;   ///< Tile area the bus 'station' part covers
	RoadStop *truck_stops;  ///< All the truck stops
	TileArea truck_station; ///< Tile area the truck 'station' part covers

	Airport airport;          ///< Tile area the airport covers
	TileArea ship_station;    ///< Tile area the ship 'station' part covers
	TileArea docking_station; ///< Tile area the docking tiles cover
	std::vector<TileIndex> docking_tiles; ///< Tile vector the docking tiles cover

	IndustryType indtype;      ///< Industry type to get the name from
	uint16_t extra_name_index; ///< Extra name index in use (or UINT16_MAX)

	BitmapTileArea catchment_tiles; ///< NOSAVE: Set of individual tiles covered by catchment area
	uint station_tiles;             ///< NOSAVE: Count of station tiles owned by this station

	StationHadVehicleOfType had_vehicle_of_type;

	uint8_t time_since_load;
	uint8_t time_since_unload;

	std::vector<Vehicle *> loading_vehicles;
	GoodsEntry goods[NUM_CARGO];  ///< Goods at this station
	CargoTypes always_accepted;       ///< Bitmask of always accepted cargo types (by houses, HQs, industry tiles when industry doesn't accept cargo)

	IndustryList industries_near; ///< Cached list of industries near the station that can accept cargo, @see DeliverGoodsToIndustry()
	Industry *industry;           ///< NOSAVE: Associated industry for neutral stations. (Rebuilt on load from Industry->st)

	CargoTypes station_cargo_history_cargoes;                                                ///< Bitmask of cargoes in station_cargo_history
	uint8_t station_cargo_history_offset;                                                    ///< Start offset in station_cargo_history cargo ring buffer
	std::vector<std::array<uint16_t, MAX_STATION_CARGO_HISTORY_DAYS>> station_cargo_history; ///< Station history of waiting cargo, dynamic range compressed (see RXCompressUint)

	Station(TileIndex tile = INVALID_TILE);
	~Station();

	void AddFacility(StationFacility new_facility_bit, TileIndex facil_xy);

	void MarkTilesDirty(bool cargo_change) const;

	void UpdateVirtCoord() override;

	void UpdateCargoHistory();

	void MoveSign(TileIndex new_xy) override;

	void AfterStationTileSetChange(bool adding, StationType type);

	uint GetPlatformLength(TileIndex tile, DiagDirection dir) const override;
	uint GetPlatformLength(TileIndex tile) const override;
	void RecomputeCatchment(bool no_clear_nearby_lists = false);
	static void RecomputeCatchmentForAll();

	uint GetCatchmentRadius() const;
	Rect GetCatchmentRectUsingRadius(uint radius) const;
	inline Rect GetCatchmentRect() const
	{
		return GetCatchmentRectUsingRadius(this->GetCatchmentRadius());
	}

	bool CatchmentCoversTown(TownID t) const;
	void AddIndustryToDeliver(Industry *ind, TileIndex tile);
	void RemoveIndustryToDeliver(Industry *ind);
	void RemoveFromAllNearbyLists();

	inline bool TileIsInCatchment(TileIndex tile) const
	{
		return this->catchment_tiles.HasTile(tile);
	}

	inline bool TileBelongsToRailStation(TileIndex tile) const override
	{
		return IsRailStationTile(tile) && GetStationIndex(tile) == this->index;
	}

	inline bool TileBelongsToRoadStop(TileIndex tile) const
	{
		return IsAnyRoadStopTile(tile) && GetStationIndex(tile) == this->index;
	}

	inline bool TileBelongsToAirport(TileIndex tile) const
	{
		return IsAirportTile(tile) && GetStationIndex(tile) == this->index;
	}

	bool IsWithinRangeOfDockingTile(TileIndex tile, uint max_distance) const;

	uint32_t GetNewGRFVariable(const ResolverObject &object, uint16_t variable, uint8_t parameter, bool *available) const override;

	void GetTileArea(TileArea *ta, StationType type) const override;
};

/** Iterator to iterate over all tiles belonging to an airport. */
class AirportTileIterator : public OrthogonalTileIterator {
private:
	const Station *st; ///< The station the airport is a part of.

public:
	/**
	 * Construct the iterator.
	 * @param st Station the airport is part of.
	 */
	AirportTileIterator(const Station *st) : OrthogonalTileIterator(st->airport), st(st)
	{
		if (!st->TileBelongsToAirport(this->tile)) ++(*this);
	}

	inline TileIterator& operator ++() override
	{
		(*this).OrthogonalTileIterator::operator++();
		while (this->tile != INVALID_TILE && !st->TileBelongsToAirport(this->tile)) {
			(*this).OrthogonalTileIterator::operator++();
		}
		return *this;
	}

	std::unique_ptr<TileIterator> Clone() const override
	{
		return std::make_unique<AirportTileIterator>(*this);
	}
};

void RebuildStationKdtree();

/**
 * Call a function on all stations that have any part of the requested area within their catchment.
 * @tparam Func The type of funcion to call
 * @param area The TileArea to check
 * @param func The function to call, must take two parameters: Station* and TileIndex and return true
 *             if coverage of that tile is acceptable for a given station or false if search should continue
 */
template<typename Func>
void ForAllStationsAroundTiles(const TileArea &ta, Func func)
{
	/* There are no stations, so we will never find anything. */
	if (Station::GetNumItems() == 0) return;

	/* Not using, or don't have a nearby stations list, so we need to scan. */
	btree::btree_set<StationID> seen_stations;

	/* Scan an area around the building covering the maximum possible station
	 * to find the possible nearby stations. */
	uint max_c = _settings_game.station.modified_catchment ? MAX_CATCHMENT : CA_UNMODIFIED;
	max_c += _settings_game.station.catchment_increase;
	TileArea ta_ext = TileArea(ta).Expand(max_c);
	for (TileIndex tile : ta_ext) {
		if (IsTileType(tile, MP_STATION)) seen_stations.insert(GetStationIndex(tile));
	}

	for (StationID stationid : seen_stations) {
		Station *st = Station::GetIfValid(stationid);
		if (st == nullptr) continue; /* Waypoint */

		/* Check if station is attached to an industry */
		if (!_settings_game.station.serve_neutral_industries && st->industry != nullptr) continue;

		/* Test if the tile is within the station's catchment */
		for (TileIndex tile : ta) {
			if (st->TileIsInCatchment(tile)) {
				if (func(st, tile)) break;
			}
		}
	}
}

#endif /* STATION_BASE_H */
