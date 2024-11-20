#include "BasicSc2Bot.h"
#include "cpp-sc2/include/sc2api/sc2_common.h"
#include "cpp-sc2/include/sc2api/sc2_typeenums.h"
#include "cpp-sc2/include/sc2lib/sc2_search.h"
#include <cmath>
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
		// TryBuildZergling();
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

		if (CountUnitType(UNIT_TYPEID::ZERG_QUEEN) < 2 && observation->GetMinerals() >= 150 && expand == false) {
			TryBuildQueen();
		}

		if (CountUnitType(UNIT_TYPEID::ZERG_QUEEN) > 0) {
			if (TryInject()) {
				std::cout << "Injecting" << std::endl;
			}
		}
	}
}

// CORE BUILDINGS / HARVESTING
// =================================================================================================
bool BasicSc2Bot::TryBuildSpawningPool() {
	const ObservationInterface *observation = Observation();

	// Check if we already have a Spawning Pool
	if (CountUnitType(UNIT_TYPEID::ZERG_SPAWNINGPOOL) > 0) {
		return false;
	}

	Point2D my_base = observation->GetStartLocation();

	// Check if we have enough resources
	if (observation->GetMinerals() >= 200) { // Spawning Pool costs 200 minerals
		const Unit *builder_drone = FindAvailableDrone();
		if (builder_drone) {
			// Define a list of potential nearby build locations
			std::vector<Point2D> build_locations = {
				my_base + Point2D(5, 5),  // Bottom Right
				my_base + Point2D(-5, 5), // Bottom Left
				my_base + Point2D(5, -5), // Top Right
				my_base + Point2D(-5, -5) // Top Left
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

	if (observation->GetMinerals() > 25) {
		// Find nearest Vespene Geyser
		const Unit *geyser = FindNearestVespeneGeyser(observation->GetStartLocation());
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
	Point2D my_base = observation->GetStartLocation();
	Point2D natural_expansion_pos = FindNaturalExpansionLocation(my_base);

	// get  available drone to build the hatchery
	const Unit *builder_drone = FindAvailableDrone();
	if (!builder_drone) {
		return false;
	}

	// verify that the location is buildable
	if (Query()->Placement(ABILITY_ID::BUILD_HATCHERY, natural_expansion_pos, builder_drone)) {
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
	const ObservationInterface *observation = Observation();

	// Check if enough minerals are available for a Queen.
	if (observation->GetMinerals() >= 150) {
		for (const auto &unit : observation->GetUnits(Unit::Alliance::Self)) {
			if (unit->unit_type == UNIT_TYPEID::ZERG_HATCHERY ||
				unit->unit_type == UNIT_TYPEID::ZERG_LAIR ||
				unit->unit_type == UNIT_TYPEID::ZERG_HIVE) {
				
				if (!unit->orders.empty()) {
					Actions()->UnitCommand(unit, ABILITY_ID::STOP);
				}
				
				Actions()->UnitCommand(unit, ABILITY_ID::TRAIN_QUEEN);
				return true;
			}
		}
	}
	return false; // Not enough minerals or no eligible Hatchery/Lair/Hive found.
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
	QueryInterface *query = Query();
	const ObservationInterface *observation = Observation();
	std::vector<Point3D> expansion_locations = sc2::search::CalculateExpansionLocations(observation, query);
	Point3D my_base = observation->GetStartLocation();
	Point3D min = Point3D(1000, 1000, 1000);
	for (auto it : expansion_locations) {
		if (abs(my_base.x - it.x) + abs(my_base.y - it.y) < abs(my_base.x - min.x) + abs(my_base.y - min.y)) {
			min = it;
		}
	}
	return min;
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

bool BasicSc2Bot::TryInject() {
	const ObservationInterface *observation = Observation();

	// Iterate through all Hatcheries and Lairs
	for (const auto &unit : observation->GetUnits(Unit::Alliance::Self)) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_HATCHERY ||
			unit->unit_type == UNIT_TYPEID::ZERG_LAIR) {

			// Check if the Hatchery is eligible for an injection.
			bool alreadyInjected = false;
			for (const auto &order : unit->orders) {
				if (order.ability_id == ABILITY_ID::EFFECT_INJECTLARVA) {
					alreadyInjected = true;
					break;
				}
			}

			if (!alreadyInjected) {
				// Find a Queen close to this Hatchery.
				const Unit *queen = nullptr;
				float closestDistance = std::numeric_limits<float>::max();

				for (const auto &unitQueen : observation->GetUnits(Unit::Alliance::Self)) {
					if (unitQueen->unit_type == UNIT_TYPEID::ZERG_QUEEN &&
						unitQueen->energy >= 25) { // Queens need at least 25 energy to inject.
						float distance = Distance2D(unit->pos, unitQueen->pos);
						if (distance < closestDistance) {
							closestDistance = distance;
							queen = unitQueen;
						}
					}
				}

				if (queen) {
					// Command the Queen to inject the Hatchery.
					Actions()->UnitCommand(queen, ABILITY_ID::EFFECT_INJECTLARVA, unit);
					return true; // Successfully injected one Hatchery.
				}
			}
		}
	}

	return false; // No eligible Hatchery or available Queen found.
}