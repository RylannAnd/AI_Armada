#include "BasicSc2Bot.h"
#include "cpp-sc2/include/sc2api/sc2_common.h"
#include "cpp-sc2/include/sc2api/sc2_typeenums.h"
#include <iostream>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_map_info.h>
#include <sc2api/sc2_unit_filters.h>

using namespace sc2;

void BasicSc2Bot::OnGameStart() { return; }
// ./BasicSc2Bot.exe -c -a zerg -d Hard -m CactusValleyLE.SC2Map
void BasicSc2Bot::OnStep() {
	const ObservationInterface *observation = Observation();

	// Find enemy base
	static Point2D enemy_base = Point2D(0, 0);
	if (enemy_base == Point2D(0, 0)) {
		enemy_base = FindEnemyBase();
	} else if (CountUnitType(UNIT_TYPEID::ZERG_ZERGLING) >= 8) {
		AttackWithZerglings(enemy_base);
	}

	static int overlord_count = 0;
	int required_overlords = (observation->GetFoodUsed() + 8) / 8;
	if ((observation->GetFoodUsed() >= 13 && overlord_count == 0 && observation->GetMinerals() >= 100) || (overlord_count > 0 && overlord_count < required_overlords)) {
		TrySpawnOverlord();
		overlord_count++;
	}

	static int drone_cap = 17;
	if (CountUnitType(UNIT_TYPEID::ZERG_DRONE) < drone_cap) {
		TryBuildDrone();
	}

	if (CountUnitType(UNIT_TYPEID::ZERG_ZERGLING) < 8) {
		TryBuildZergling();
	}

	static bool spawn_pool = true;
	static bool extractor = true;
	static bool expand = true;

	if (observation->GetFoodUsed() >= 17) {
		if (extractor && observation->GetMinerals() >= 25) {
			std::cout << "building extractor" << std::endl;
			TryBuildExtractor();
			extractor = false;
		}

		if (spawn_pool && observation->GetMinerals() >= 200) {
			std::cout << "building pool" << std::endl;
			TryBuildSpawningPool();
			spawn_pool = false;
		}

		if (expand && observation->GetMinerals() >= 300) {
			std::cout << "expanding" << std::endl;
			TryBuildHatcheryInNatural();
			expand = false;
		}
	}
}

//  void BasicSc2Bot::OnUnitIdle(const Unit* unit) {
//     switch (unit->unit_type.ToType()) {
//         case UNIT_TYPEID::ZERG_DRONE: {
//             // This is where the newly spawned Drone would be detected as
//             idle.
//             // Override its default behavior to keep it idle.
//             Actions()->UnitCommand(unit, ABILITY_ID::STOP);
//             break;
//         }
//         default:
//             break;
//     }
// }

// CORE BUILDINGS / HARVESTING
// =================================================================================================
bool BasicSc2Bot::TryBuildSpawningPool() {
	const ObservationInterface *observation = Observation();

	// Check if we already have a Spawning Pool
	if (CountUnitType(UNIT_TYPEID::ZERG_SPAWNINGPOOL) > 0) {
		return false;
	}

	const Unit *hatchery = nullptr;
	for (const auto &unit : observation->GetUnits()) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_HATCHERY && unit->alliance == Unit::Alliance::Self) {
			hatchery = unit;
			break;
		}
	}

	// Check if we have enough resources
	if (observation->GetMinerals() >= 200) { // Spawning Pool costs 200 minerals
		const Unit *builder_drone = FindAvailableDrone();
		if (builder_drone) {
			// Define a list of potential nearby build locations
			std::vector<Point2D> build_locations = {
				hatchery->pos + Point2D(3, 3),	// Bottom Right
				hatchery->pos + Point2D(-3, 3), // Bottom Left
				hatchery->pos + Point2D(3, -3), // Top Right
				hatchery->pos + Point2D(-3, -3) // Top Left
			};

			for (const auto &location : build_locations) {
				if (Query()->Placement(ABILITY_ID::BUILD_SPAWNINGPOOL, location)) {
					Actions()->UnitCommand(builder_drone, ABILITY_ID::BUILD_SPAWNINGPOOL, location);
					return true;
				}
			}
		}
	}
	return false;
}

bool BasicSc2Bot::TryBuildExtractor() {
	const ObservationInterface *observation = Observation();

	// Find an unoccupied Vespene Geyser near a Hatchery
	const Unit *hatchery = nullptr;
	for (const auto &unit : observation->GetUnits()) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_HATCHERY && unit->alliance == Unit::Alliance::Self) {
			hatchery = unit;
			break;
		}
	}

	if (!hatchery) {
		return false;
	}

	if (observation->GetMinerals() > 25) {
		// Find nearest Vespene Geyser
		const Unit *geyser = FindNearestVespeneGeyser(hatchery->pos);
		const Unit *builder_drone = FindAvailableDrone();
		if (geyser && builder_drone) {
			Actions()->UnitCommand(builder_drone, ABILITY_ID::BUILD_EXTRACTOR, geyser);
			// const Unit *gas_drone = FindAvailableDrone();
			//
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::TryBuildHatcheryInNatural() {
	const ObservationInterface *observation = Observation();

	if (observation->GetMinerals() < 300) { // hatchery cost
		return false;
	}

	// get position of main hatchery as reference
	const Unit *main_hatchery;
	for (const auto &unit : observation->GetUnits()) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_HATCHERY && unit->alliance == Unit::Alliance::Self) {
			main_hatchery = unit;
			break;
		}
	}
	if (!main_hatchery) {
		return false;
	}

	Point2D natural_expansion_pos = FindNaturalExpansionLocation(main_hatchery->pos);

	// get  available drone to build the hatchery
	const Unit *builder_drone = FindAvailableDrone();
	if (!builder_drone) {
		return false;
	}

	// verify that the location is buildable
	if (Query()->Placement(ABILITY_ID::BUILD_HATCHERY, natural_expansion_pos)) {
		std::cout << "Building Hatchery at natural expansion location" << std::endl;
		Actions()->UnitCommand(builder_drone, ABILITY_ID::BUILD_HATCHERY, natural_expansion_pos);
		return true;
	}

	return false; // unable to find a suitable location
}
// UNIT PRODUCTION
// ==================================================================================================
bool BasicSc2Bot::TryBuildZergling() {
	const ObservationInterface *observation = Observation();
	if (CountUnitType(UNIT_TYPEID::ZERG_SPAWNINGPOOL) && observation->GetMinerals() >= 50) {
		const Unit *larva = FindNearestLarva();
		if (larva) {
			Actions()->UnitCommand(larva, ABILITY_ID::TRAIN_ZERGLING);
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::TryBuildDrone() {
	const ObservationInterface *observation = Observation();

	// Build a drone when we have two overlord
	if (observation->GetMinerals() >= 50 && observation->GetFoodUsed() <= 17 && CountUnitType(UNIT_TYPEID::ZERG_DRONE) < 17) {
		// Find a larva to morph into a Drone.
		const Unit *larva = FindNearestLarva();
		if (larva) {
			Actions()->UnitCommand(larva, ABILITY_ID::TRAIN_DRONE);
			// Actions()->UnitCommand(unit, ABILITY_ID::STOP);
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::TrySpawnOverlord() {
	const ObservationInterface *observation = Observation();

	if (observation->GetMinerals() >= 100) { // Overlord costs 100 minerals
		// Find a larva to morph into an Overlord.
		const Unit *larva = FindNearestLarva();
		if (larva) {
			Actions()->UnitCommand(larva, ABILITY_ID::TRAIN_OVERLORD);
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::TryBuildQueen() {
	return true;
}

// FIND UNITS
// ===========================================================================================================
const Unit *BasicSc2Bot::FindNearestLarva() {
	Units units = Observation()->GetUnits(Unit::Alliance::Self);
	for (const auto &unit : units) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_LARVA) {
			return unit;
		}
	}
	return nullptr;
}

const Unit *BasicSc2Bot::FindAvailableDrone() {
	const ObservationInterface *observation = Observation();
	Units units = observation->GetUnits(Unit::Alliance::Self);

	for (const auto &unit : units) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_DRONE && !unit->orders.empty()) {
			// Ensure the drone's last order is to gather resources
			const auto &last_order = unit->orders.back();
			if (last_order.ability_id == ABILITY_ID::HARVEST_GATHER) {
				Actions()->UnitCommand(unit, ABILITY_ID::STOP);
				return unit;
			}
		}
	}
	return nullptr;
}

int BasicSc2Bot::CountUnitType(UNIT_TYPEID unit_type) {
	Units units = Observation()->GetUnits(Unit::Alliance::Self);
	int count = 0;
	for (const auto &unit : units) {
		if (unit->unit_type == unit_type) {
			count++;
		}
	}
	return count;
}

// FIND LOCATIONS
// ===================================================================================================================
const Unit *BasicSc2Bot::FindNearestVespeneGeyser(const Point2D &start) {
	const ObservationInterface *observation = Observation();
	const Unit *nearest_geyser = nullptr;
	float min_distance = std::numeric_limits<float>::max();

	for (const auto &unit : observation->GetUnits()) {
		if (unit->unit_type == UNIT_TYPEID::NEUTRAL_VESPENEGEYSER) {
			float distance = DistanceSquared2D(unit->pos, start);
			if (distance < min_distance) {
				min_distance = distance;
				nearest_geyser = unit;
			}
		}
	}
	return nearest_geyser;
}

Point2D BasicSc2Bot::FindNaturalExpansionLocation(const Point2D &main_hatchery_pos) {
	// Start with an approximate offset based on main base spawn
	Point2D start_location;

	if (Distance2D(main_hatchery_pos, Point2D(20, 20)) < 10) {
		// Top-left spawn
		start_location = main_hatchery_pos + Point2D(20, -10);
	} else if (Distance2D(main_hatchery_pos, Point2D(132, 20)) < 10) {
		// Top-right spawn
		start_location = main_hatchery_pos + Point2D(-20, -10);
	} else if (Distance2D(main_hatchery_pos, Point2D(20, 132)) < 10) {
		// Bottom-left spawn
		start_location = main_hatchery_pos + Point2D(20, 10);
	} else if (Distance2D(main_hatchery_pos, Point2D(132, 132)) < 10) {
		// Bottom-right spawn
		start_location = main_hatchery_pos + Point2D(-20, 10);
	} else {
		// Default offset if no spawn detected
		start_location = main_hatchery_pos + Point2D(15, 15);
	}

	// Loop through potential locations in a small grid around the start_location
	int search_radius = 10; // Adjust as needed
	for (int dx = -search_radius; dx <= search_radius; dx++) {
		for (int dy = -search_radius; dy <= search_radius; dy++) {
			Point2D test_location = start_location + Point2D(dx, dy);

			// Check if we can place a Hatchery at this location
			if (Query()->Placement(ABILITY_ID::BUILD_HATCHERY, test_location)) {
				return test_location;
			}
		}
	}

	// If no valid location is found, return the original approximate position
	return start_location;
}
}

// ATTACKING / SCOUTING
// ======================================================================================================================
void BasicSc2Bot::AttackWithZerglings(Point2D target) {
	auto zerglings = Observation()->GetUnits([&](const Unit &unit) {
		return unit.unit_type == UNIT_TYPEID::ZERG_ZERGLING && unit.alliance == Unit::Alliance::Self;
	});

	if (target != Point2D(0, 0)) {
		for (const auto &zergling : zerglings) {
			Actions()->UnitCommand(zergling, ABILITY_ID::ATTACK, target);
		}
	}
}

// For later scouting use
Point2D BasicSc2Bot::FindEnemyBase() {
	for (const auto &enemy_struct : Observation()->GetUnits(Unit::Alliance::Enemy)) {
		if (enemy_struct->unit_type == UNIT_TYPEID::ZERG_HATCHERY ||
			enemy_struct->unit_type == UNIT_TYPEID::PROTOSS_NEXUS ||
			enemy_struct->unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTER) {
			return enemy_struct->pos;
		}
	}
	return Point2D(0, 0);
}