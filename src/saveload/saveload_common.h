/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file saveload_common.h Common functions/types for saving and loading games. */

#ifndef SAVELOAD_COMMON_H
#define SAVELOAD_COMMON_H

#include "../strings_type.h"
#include "../core/span_type.hpp"

struct SaveLoad;

/** A table of SaveLoad entries. */
using SaveLoadTable = span<const SaveLoad>;

namespace upstream_sl {
	struct SaveLoad;

	/** A table of SaveLoad entries. */
	using SaveLoadTable = span<const SaveLoad>;
}

/** SaveLoad versions
 * Previous savegame versions, the trunk revision where they were
 * introduced and the released version that had that particular
 * savegame version.
 * Up to savegame version 18 there is a minor version as well.
 *
 * Older entries keep their original numbering.
 *
 * Newer entries should use a descriptive labels, numeric version
 * and PR can be added to comment.
 *
 * Note that this list must not be reordered.
 */
enum SaveLoadVersion : uint16 {
	SL_MIN_VERSION,                         ///< First savegame version

	SLV_1,                                  ///<   1.0         0.1.x, 0.2.x
	SLV_2,                                  /**<   2.0         0.3.0
	                                         *     2.1         0.3.1, 0.3.2 */
	SLV_3,                                  ///<   3.x         lost
	SLV_4,                                  /**<   4.0     1
	                                         *     4.1   122   0.3.3, 0.3.4
	                                         *     4.2  1222   0.3.5
	                                         *     4.3  1417
	                                         *     4.4  1426 */

	SLV_5,                                  /**<   5.0  1429
	                                         *     5.1  1440
	                                         *     5.2  1525   0.3.6 */
	SLV_6,                                  /**<   6.0  1721
	                                         *     6.1  1768 */
	SLV_7,                                  ///<   7.0  1770
	SLV_8,                                  ///<   8.0  1786
	SLV_9,                                  ///<   9.0  1909

	SLV_10,                                 ///<  10.0  2030
	SLV_11,                                 /**<  11.0  2033
	                                         *    11.1  2041 */
	SLV_12,                                 ///<  12.1  2046
	SLV_13,                                 ///<  13.1  2080   0.4.0, 0.4.0.1
	SLV_14,                                 ///<  14.0  2441

	SLV_15,                                 ///<  15.0  2499
	SLV_16,                                 /**<  16.0  2817
	                                         *    16.1  3155 */
	SLV_17,                                 /**<  17.0  3212
	                                         *    17.1  3218 */
	SLV_18,                                 ///<  18    3227
	SLV_19,                                 ///<  19    3396

	SLV_20,                                 ///<  20    3403
	SLV_21,                                 ///<  21    3472   0.4.x
	SLV_22,                                 ///<  22    3726
	SLV_23,                                 ///<  23    3915
	SLV_24,                                 ///<  24    4150

	SLV_25,                                 ///<  25    4259
	SLV_26,                                 ///<  26    4466
	SLV_27,                                 ///<  27    4757
	SLV_28,                                 ///<  28    4987
	SLV_29,                                 ///<  29    5070

	SLV_30,                                 ///<  30    5946
	SLV_31,                                 ///<  31    5999
	SLV_32,                                 ///<  32    6001
	SLV_33,                                 ///<  33    6440
	SLV_34,                                 ///<  34    6455

	SLV_35,                                 ///<  35    6602
	SLV_36,                                 ///<  36    6624
	SLV_37,                                 ///<  37    7182
	SLV_38,                                 ///<  38    7195
	SLV_39,                                 ///<  39    7269

	SLV_40,                                 ///<  40    7326
	SLV_41,                                 ///<  41    7348   0.5.x
	SLV_42,                                 ///<  42    7573
	SLV_43,                                 ///<  43    7642
	SLV_44,                                 ///<  44    8144

	SLV_45,                                 ///<  45    8501
	SLV_46,                                 ///<  46    8705
	SLV_47,                                 ///<  47    8735
	SLV_48,                                 ///<  48    8935
	SLV_49,                                 ///<  49    8969

	SLV_50,                                 ///<  50    8973
	SLV_51,                                 ///<  51    8978
	SLV_52,                                 ///<  52    9066
	SLV_53,                                 ///<  53    9316
	SLV_54,                                 ///<  54    9613

	SLV_55,                                 ///<  55    9638
	SLV_56,                                 ///<  56    9667
	SLV_57,                                 ///<  57    9691
	SLV_58,                                 ///<  58    9762
	SLV_59,                                 ///<  59    9779

	SLV_60,                                 ///<  60    9874
	SLV_61,                                 ///<  61    9892
	SLV_62,                                 ///<  62    9905
	SLV_63,                                 ///<  63    9956
	SLV_64,                                 ///<  64   10006

	SLV_65,                                 ///<  65   10210
	SLV_66,                                 ///<  66   10211
	SLV_67,                                 ///<  67   10236
	SLV_68,                                 ///<  68   10266
	SLV_69,                                 ///<  69   10319

	SLV_70,                                 ///<  70   10541
	SLV_71,                                 ///<  71   10567
	SLV_72,                                 ///<  72   10601
	SLV_73,                                 ///<  73   10903
	SLV_74,                                 ///<  74   11030

	SLV_75,                                 ///<  75   11107
	SLV_76,                                 ///<  76   11139
	SLV_77,                                 ///<  77   11172
	SLV_78,                                 ///<  78   11176
	SLV_79,                                 ///<  79   11188

	SLV_80,                                 ///<  80   11228
	SLV_81,                                 ///<  81   11244
	SLV_82,                                 ///<  82   11410
	SLV_83,                                 ///<  83   11589
	SLV_84,                                 ///<  84   11822

	SLV_85,                                 ///<  85   11874
	SLV_86,                                 ///<  86   12042
	SLV_87,                                 ///<  87   12129
	SLV_88,                                 ///<  88   12134
	SLV_89,                                 ///<  89   12160

	SLV_90,                                 ///<  90   12293
	SLV_91,                                 ///<  91   12347
	SLV_92,                                 ///<  92   12381   0.6.x
	SLV_93,                                 ///<  93   12648
	SLV_94,                                 ///<  94   12816

	SLV_95,                                 ///<  95   12924
	SLV_96,                                 ///<  96   13226
	SLV_97,                                 ///<  97   13256
	SLV_98,                                 ///<  98   13375
	SLV_99,                                 ///<  99   13838

	SLV_100,                                ///< 100   13952
	SLV_101,                                ///< 101   14233
	SLV_102,                                ///< 102   14332
	SLV_103,                                ///< 103   14598
	SLV_104,                                ///< 104   14735

	SLV_105,                                ///< 105   14803
	SLV_106,                                ///< 106   14919
	SLV_107,                                ///< 107   15027
	SLV_108,                                ///< 108   15045
	SLV_109,                                ///< 109   15075

	SLV_110,                                ///< 110   15148
	SLV_111,                                ///< 111   15190
	SLV_112,                                ///< 112   15290
	SLV_113,                                ///< 113   15340
	SLV_114,                                ///< 114   15601

	SLV_115,                                ///< 115   15695
	SLV_116,                                ///< 116   15893   0.7.x
	SLV_117,                                ///< 117   16037
	SLV_118,                                ///< 118   16129
	SLV_119,                                ///< 119   16242

	SLV_120,                                ///< 120   16439
	SLV_121,                                ///< 121   16694
	SLV_122,                                ///< 122   16855
	SLV_123,                                ///< 123   16909
	SLV_124,                                ///< 124   16993

	SLV_125,                                ///< 125   17113
	SLV_126,                                ///< 126   17433
	SLV_127,                                ///< 127   17439
	SLV_128,                                ///< 128   18281
	SLV_129,                                ///< 129   18292

	SLV_130,                                ///< 130   18404
	SLV_131,                                ///< 131   18481
	SLV_132,                                ///< 132   18522
	SLV_133,                                ///< 133   18674
	SLV_134,                                ///< 134   18703

	SLV_135,                                ///< 135   18719
	SLV_136,                                ///< 136   18764
	SLV_137,                                ///< 137   18912
	SLV_138,                                ///< 138   18942   1.0.x
	SLV_139,                                ///< 139   19346

	SLV_140,                                ///< 140   19382
	SLV_141,                                ///< 141   19799
	SLV_142,                                ///< 142   20003
	SLV_143,                                ///< 143   20048
	SLV_144,                                ///< 144   20334

	SLV_145,                                ///< 145   20376
	SLV_146,                                ///< 146   20446
	SLV_147,                                ///< 147   20621
	SLV_148,                                ///< 148   20659
	SLV_149,                                ///< 149   20832

	SLV_150,                                ///< 150   20857
	SLV_151,                                ///< 151   20918
	SLV_152,                                ///< 152   21171
	SLV_153,                                ///< 153   21263
	SLV_154,                                ///< 154   21426

	SLV_155,                                ///< 155   21453
	SLV_156,                                ///< 156   21728
	SLV_157,                                ///< 157   21862
	SLV_158,                                ///< 158   21933
	SLV_159,                                ///< 159   21962

	SLV_160,                                ///< 160   21974   1.1.x
	SLV_161,                                ///< 161   22567
	SLV_162,                                ///< 162   22713
	SLV_163,                                ///< 163   22767
	SLV_164,                                ///< 164   23290

	SLV_165,                                ///< 165   23304
	SLV_166,                                ///< 166   23415
	SLV_167,                                ///< 167   23504
	SLV_168,                                ///< 168   23637
	SLV_169,                                ///< 169   23816

	SLV_170,                                ///< 170   23826
	SLV_171,                                ///< 171   23835
	SLV_172,                                ///< 172   23947
	SLV_173,                                ///< 173   23967   1.2.0-RC1
	SLV_174,                                ///< 174   23973   1.2.x

	SLV_175,                                ///< 175   24136
	SLV_176,                                ///< 176   24446
	SLV_177,                                ///< 177   24619
	SLV_178,                                ///< 178   24789
	SLV_179,                                ///< 179   24810

	SLV_180,                                ///< 180   24998   1.3.x
	SLV_181,                                ///< 181   25012
	SLV_182,                                ///< 182   25115 FS#5492, r25259, r25296 Goal status
	SLV_183,                                ///< 183   25363 Cargodist
	SLV_184,                                ///< 184   25508 Unit localisation split

	SLV_185,                                ///< 185   25620 Storybooks
	SLV_186,                                ///< 186   25833 Objects storage
	SLV_187,                                ///< 187   25899 Linkgraph - restricted flows
	SLV_188,                                ///< 188   26169 v1.4  FS#5831 Unify RV travel time
	SLV_189,                                ///< 189   26450 Hierarchical vehicle subgroups

	SLV_190,                                ///< 190   26547 Separate order travel and wait times
	SLV_191,                                ///< 191   26636 FS#6026 Fix disaster vehicle storage (No bump)
	                                        ///< 191   26646 FS#6041 Linkgraph - store locations
	SLV_192,                                ///< 192   26700 FS#6066 Fix saving of order backups
	SLV_193,                                ///< 193   26802
	SLV_194,                                ///< 194   26881 v1.5

	SLV_195,                                ///< 195   27572 v1.6.1
	SLV_196,                                ///< 196   27778 v1.7
	SLV_197,                                ///< 197   27978 v1.8
	SLV_198,                                ///< 198  PR#6763 Switch town growth rate and counter to actual game ticks
	SLV_EXTEND_CARGOTYPES,                  ///< 199  PR#6802 Extend cargotypes to 64

	SLV_EXTEND_RAILTYPES,                   ///< 200  PR#6805 Extend railtypes to 64, adding uint16 to map array.
	SLV_EXTEND_PERSISTENT_STORAGE,          ///< 201  PR#6885 Extend NewGRF persistent storages.
	SLV_EXTEND_INDUSTRY_CARGO_SLOTS,        ///< 202  PR#6867 Increase industry cargo slots to 16 in, 16 out
	SLV_SHIP_PATH_CACHE,                    ///< 203  PR#7072 Add path cache for ships
	SLV_SHIP_ROTATION,                      ///< 204  PR#7065 Add extra rotation stages for ships.

	SLV_GROUP_LIVERIES,                     ///< 205  PR#7108 Livery storage change and group liveries.
	SLV_SHIPS_STOP_IN_LOCKS,                ///< 206  PR#7150 Ship/lock movement changes.
	SLV_FIX_CARGO_MONITOR,                  ///< 207  PR#7175 v1.9  Cargo monitor data packing fix to support 64 cargotypes.
	SLV_TOWN_CARGOGEN,                      ///< 208  PR#6965 New algorithms for town building cargo generation.
	SLV_SHIP_CURVE_PENALTY,                 ///< 209  PR#7289 Configurable ship curve penalties.

	SLV_SERVE_NEUTRAL_INDUSTRIES,           ///< 210  PR#7234 Company stations can serve industries with attached neutral stations.
	SLV_ROADVEH_PATH_CACHE,                 ///< 211  PR#7261 Add path cache for road vehicles.
	SLV_REMOVE_OPF,                         ///< 212  PR#7245 Remove OPF.
	SLV_TREES_WATER_CLASS,                  ///< 213  PR#7405 WaterClass update for tree tiles.
	SLV_ROAD_TYPES,                         ///< 214  PR#6811 NewGRF road types.

	SLV_SCRIPT_MEMLIMIT,                    ///< 215  PR#7516 Limit on AI/GS memory consumption.
	SLV_MULTITILE_DOCKS,                    ///< 216  PR#7380 Multiple docks per station.
	SLV_TRADING_AGE,                        ///< 217  PR#7780 Configurable company trading age.
	SLV_ENDING_YEAR,                        ///< 218  PR#7747 v1.10  Configurable ending year.
	SLV_REMOVE_TOWN_CARGO_CACHE,            ///< 219  PR#8258 Remove town cargo acceptance and production caches.

	/* Patchpacks for a while considered it a good idea to jump a few versions
	 * above our version for their savegames. But as time continued, this gap
	 * has been closing, up to the point we would start to reuse versions from
	 * their patchpacks. This is not a problem from our perspective: the
	 * savegame will simply fail to load because they all contain chunks we
	 * cannot digest. But, this gives for ugly errors. As we have plenty of
	 * versions anyway, we simply skip the versions we know belong to
	 * patchpacks. This way we can present the user with a clean error
	 * indicate they are loading a savegame from a patchpack.
	 * For future patchpack creators: please follow a system like JGRPP, where
	 * the version is masked with 0x8000, and the true version is stored in
	 * its own chunk with feature toggles.
	 */
	SLV_START_PATCHPACKS,                   ///< 220  First known patchpack to use a version just above ours.
	SLV_END_PATCHPACKS = 286,               ///< 286  Last known patchpack to use a version just above ours.

	SLV_GS_INDUSTRY_CONTROL,                ///< 287  PR#7912 and PR#8115 GS industry control.
	SLV_VEH_MOTION_COUNTER,                 ///< 288  PR#8591 Desync safe motion counter
	SLV_INDUSTRY_TEXT,                      ///< 289  PR#8576 v1.11.0-RC1  Additional GS text for industries.
	SLV_MAPGEN_SETTINGS_REVAMP,             ///< 290  PR#8891 v1.11  Revamp of some mapgen settings (snow coverage, desert coverage, heightmap height, custom terrain type).
	SLV_GROUP_REPLACE_WAGON_REMOVAL,        ///< 291  PR#7441 Per-group wagon removal flag.
	SLV_CUSTOM_SUBSIDY_DURATION,            ///< 292  PR#9081 Configurable subsidy duration.

	/* upstream savegame versions follow */

	SLV_SAVELOAD_LIST_LENGTH,               ///< 293  PR#9374 Consistency in list length with SL_STRUCT / SL_STRUCTLIST / SL_DEQUE / SL_REFLIST.
	SLV_RIFF_TO_ARRAY,                      ///< 294  PR#9375 Changed many CH_RIFF chunks to CH_ARRAY chunks.

	SLV_TABLE_CHUNKS,                       ///< 295  PR#9322 Introduction of CH_TABLE and CH_SPARSE_TABLE.
	SLV_SCRIPT_INT64,                       ///< 296  PR#9415 SQInteger is 64bit but was saved as 32bit.
	SLV_LINKGRAPH_TRAVEL_TIME,              ///< 297  PR#9457 v12.0-RC1  Store travel time in the linkgraph.
	SLV_DOCK_DOCKINGTILES,                  ///< 298  PR#9578 All tiles around docks may be docking tiles.
	SLV_REPAIR_OBJECT_DOCKING_TILES,        ///< 299  PR#9594 v12.0  Fixing issue with docking tiles overlapping objects.

	SLV_U64_TICK_COUNTER,                   ///< 300  PR#10035 Make _tick_counter 64bit to avoid wrapping.
	SLV_LAST_LOADING_TICK,                  ///< 301  PR#9693 Store tick of last loading for vehicles.
	SLV_MULTITRACK_LEVEL_CROSSINGS,         ///< 302  PR#9931 v13.0  Multi-track level crossings.
	SLV_NEWGRF_ROAD_STOPS,                  ///< 303  PR#10144 NewGRF road stops.
	SLV_LINKGRAPH_EDGES,                    ///< 304  PR#10314 Explicitly store link graph edges destination.

	SLV_VELOCITY_NAUTICAL,                  ///< 305  PR#10594 Separation of land and nautical velocity (knots!)
	SLV_CONSISTENT_PARTIAL_Z,               ///< 306  PR#10570 Conversion from an inconsistent partial Z calculation for slopes, to one that is (more) consistent.
	SLV_MORE_CARGO_AGE,                     ///< 307  PR#10596 Track cargo age for a longer period.
	SLV_LINKGRAPH_SECONDS,                  ///< 308  PR#10610 Store linkgraph update intervals in seconds instead of days.
	SLV_AI_START_DATE,                      ///< 309  PR#10653 Removal of individual AI start dates and added a generic one.

	SL_MAX_VERSION,                         ///< Highest possible saveload version

	SL_SPRING_2013_v2_0_102 = 220,
	SL_SPRING_2013_v2_1_108 = 221,
	SL_SPRING_2013_v2_1_147 = 222,
	SL_SPRING_2013_v2_3_XXX = 223,
	SL_SPRING_2013_v2_3_b3 = 224,
	SL_SPRING_2013_v2_3_b4 = 225,
	SL_SPRING_2013_v2_3_b5 = 226,
	SL_SPRING_2013_v2_4 = 227,
	SL_TRACE_RESTRICT_2000 = 2000,
	SL_TRACE_RESTRICT_2001 = 2001,
	SL_TRACE_RESTRICT_2002 = 2002,
	SL_JOKER_1_19 = 278,
	SL_JOKER_1_20 = 279,
	SL_JOKER_1_21 = 280,
	SL_JOKER_1_22 = 281,
	SL_JOKER_1_23 = 282,
	SL_JOKER_1_24 = 283,
	SL_JOKER_1_25 = 284,
	SL_JOKER_1_26 = 285,
	SL_JOKER_1_27 = 286,
	SL_CHILLPP_201 = 201,
	SL_CHILLPP_232 = 232,
	SL_CHILLPP_233 = 233,
};

byte SlReadByte();
void SlWriteByte(byte b);

int SlReadUint16();
uint32 SlReadUint32();
uint64 SlReadUint64();

void SlWriteUint16(uint16 v);
void SlWriteUint32(uint32 v);
void SlWriteUint64(uint64 v);

void SlSkipBytes(size_t length);

size_t SlGetBytesRead();
size_t SlGetBytesWritten();

void NORETURN SlError(StringID string, std::string extra_msg = {});
void NORETURN SlErrorCorrupt(std::string msg);
void NORETURN CDECL SlErrorCorruptFmt(const char *format, ...) WARN_FORMAT(1, 2);

bool SaveLoadFileTypeIsScenario();

#endif
