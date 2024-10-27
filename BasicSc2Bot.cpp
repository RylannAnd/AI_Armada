#include "BasicSc2Bot.h"
#include "cpp-sc2/include/sc2api/sc2_common.h"
#include "cpp-sc2/include/sc2api/sc2_typeenums.h"
#include <iostream>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_unit_filters.h>

using namespace sc2;

void BasicSc2Bot::OnGameStart() { return; }

void BasicSc2Bot::OnStep() {
	const ObservationInterface *observation = Observation();

	static int overlord_count = 0;
	if (observation->GetFoodUsed() >= 13 && overlord_count == 0 && observation->GetMinerals() >= 100) {
		TrySpawnOverlord();
		overlord_count++;
	}

	TryBuildDrone();

	static int drone_count = 0;
	if (drone_count == 0 && observation->GetFoodUsed() == 17) {
		std::cout << " building first extra drone " << std::endl;
		drone_count++;
	}

	if (drone_count == 1 && observation->GetFoodUsed() == 17) { // first drone extractor
		TryBuildExtractor();
		std::cout << " building second extra drone " << std::endl;
		drone_count++;
	}

	if (drone_count == 2 && observation->GetFoodUsed() == 17 && observation->GetMinerals() >= 200) { // second drone pool
		TryBuildSpawningPool();
		drone_count++;
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
				hatchery->pos + Point2D(5, 5),	// Bottom Right
				hatchery->pos + Point2D(-5, 5), // Bottom Left
				hatchery->pos + Point2D(5, -5), // Top Right
				hatchery->pos + Point2D(-5, -5) // Top Left
			};

			for (const auto &location : build_locations) {
				if (Query()->Placement(ABILITY_ID::BUILD_SPAWNINGPOOL, location)) {
					std::cout << "Building Spawning Pool at location near Hatchery" << std::endl;
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
			std::cout << "Building Extractor..." << std::endl;
			Actions()->UnitCommand(builder_drone, ABILITY_ID::BUILD_EXTRACTOR, geyser);
			// const Unit *gas_drone = FindAvailableDrone();
			//
			return true;
		}
	}
	return false;
}

// UNIT PRODUCTION
// ==================================================================================================
bool BasicSc2Bot::TryBuildZergling() {
	const ObservationInterface *observation = Observation();
	if (observation->GetMinerals() >= 50) {
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

// ATTACKING / SCOUTING
// ======================================================================================================================