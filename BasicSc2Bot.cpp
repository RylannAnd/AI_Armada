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

	static int overlord_count = 0;
	int required_overlords = (observation->GetFoodUsed() + 8) / 8;
	if ((observation->GetFoodUsed() >= 13 && overlord_count == 0 && observation->GetMinerals() >= 100) || (overlord_count > 0 && overlord_count < required_overlords)) {
		TrySpawnOverlord();
		overlord_count++;
	}

	static int drone_cap = 20;
	if (CountUnitType(UNIT_TYPEID::ZERG_DRONE) < drone_cap) {
		TryBuildDrone();
	}

	static bool spawn_pool = true;
	static bool create_extractor = true;
	static bool expand = true;
	static int num_zergling_upgrades = 0;

	if (observation->GetFoodUsed() >= 17) {
		if (create_extractor && observation->GetMinerals() >= 25) {
			if (TryBuildExtractor()) {
				create_extractor = false;
				std::cout << "building extractor" << std::endl;
			}
		}

		// Build Spawning Pool
		if (spawn_pool && observation->GetMinerals() >= 200) {
			if (TryBuildSpawningPool()) {
				spawn_pool = false;
				std::cout << "building pool" << std::endl;
			}
		}

		// Build Hatchery in Natural Expansion
		if (expand && observation->GetMinerals() >= 300) {
			if (TryBuildHatcheryInNatural()) {
				expand = false;
				std::cout << "expanding" << std::endl;
			}
		}
	}

	// ATTACKING LOGIC
	// =================================================================================================
	if (!expand) { // execute attacks and upgrades after expanding
		TryBuildZergling();
		AttackWithZerglings();
		// =================================================================================================

		// morph lair
		if (CountUnitType(UNIT_TYPEID::ZERG_LAIR) < 1) {
			TryBuildUnit(ABILITY_ID::MORPH_LAIR, UNIT_TYPEID::ZERG_HATCHERY);
		}

		// Make Queens
		if (CountUnitType(UNIT_TYPEID::ZERG_LAIR) > 0 && CountUnitType(UNIT_TYPEID::ZERG_QUEEN) < 2 && observation->GetMinerals() >= 150 && expand == false) {
			TryBuildQueen();
		}

		// extractor workers
		if (CountUnitType(UNIT_TYPEID::ZERG_EXTRACTOR) > 0) {
			// AssignExtractorWorkers();
		}

		if (CountUnitType(UNIT_TYPEID::ZERG_INFESTATIONPIT) < 1) {
			TryBuildStructure(ABILITY_ID::BUILD_INFESTATIONPIT, UNIT_TYPEID::ZERG_DRONE);
		}

		if (CountUnitType(UNIT_TYPEID::ZERG_HIVE) < 1) {
			TryBuildUnit(ABILITY_ID::MORPH_HIVE, UNIT_TYPEID::ZERG_LAIR);
		}

		// Do Injections for extra larvae after lair is built
		if (CountUnitType(UNIT_TYPEID::ZERG_LAIR) > 0 && CountUnitType(UNIT_TYPEID::ZERG_QUEEN) > 0) {
			TryInject();
		}

		// Upgrade zerling abilities
		if (num_zergling_upgrades == 0) {
			std::vector<UpgradeID> completed_upgrades = observation->GetUpgrades();

			if (std::find(completed_upgrades.begin(), completed_upgrades.end(), UPGRADE_ID::ZERGLINGMOVEMENTSPEED) != completed_upgrades.end()) {
				num_zergling_upgrades++;
			} else {
				TryBuildUnit(ABILITY_ID::RESEARCH_ZERGLINGMETABOLICBOOST, UNIT_TYPEID::ZERG_SPAWNINGPOOL);
			}
		} else if (num_zergling_upgrades == 1) {
			TryBuildUnit(ABILITY_ID::RESEARCH_ZERGLINGADRENALGLANDS, UNIT_TYPEID::ZERG_SPAWNINGPOOL);
		}
	}
}

void BasicSc2Bot::AssignExtractorWorkers() {
	Units extractors = Observation()->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_EXTRACTOR));
	const Unit *extractor = extractors[0];

	if (extractor->assigned_harvesters < 3) {
		const Unit *unit = FindAvailableDrone();
		Actions()->UnitCommand(unit, ABILITY_ID::HARVEST_GATHER, extractor);
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
	if (observation->GetMinerals() >= 50 && observation->GetFoodUsed() <= 20 && CountUnitType(UNIT_TYPEID::ZERG_DRONE) < 20) {
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

bool BasicSc2Bot::GetRandomUnit(const Unit *&unit_out, const ObservationInterface *observation, UnitTypeID unit_type) {
	Units my_units = observation->GetUnits(Unit::Alliance::Self);
	// std::random_shuffle(my_units.begin(), my_units.end()); // Doesn't work, or doesn't work well.
	for (const auto unit : my_units) {
		if (unit->unit_type == unit_type) {
			unit_out = unit;
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type) {
	const ObservationInterface *observation = Observation();

	// If a unit already is building a supply structure of this type, do nothing.
	Units units = observation->GetUnits(Unit::Alliance::Self);
	for (const auto &unit : units) {
		for (const auto &order : unit->orders) {
			if (order.ability_id == ability_type_for_structure) {
				return false;
			}
		}
	}

	// Just try a random location near the unit.
	const Unit *unit = nullptr;
	if (!GetRandomUnit(unit, observation, unit_type)) {
		return false;
	}

	float rx = GetRandomScalar();
	float ry = GetRandomScalar();

	Actions()->UnitCommand(unit, ability_type_for_structure, unit->pos + Point2D(rx, ry) * 9.0f);
	return true;
}

bool BasicSc2Bot::TryBuildUnit(AbilityID ability_type_for_unit, UnitTypeID unit_type) {
	const ObservationInterface *observation = Observation();

	// If we are at supply cap, don't build anymore units, unless its an overlord.
	if (observation->GetFoodUsed() >= observation->GetFoodCap() && ability_type_for_unit != ABILITY_ID::TRAIN_OVERLORD) {
		return false;
	}
	const Unit *unit = nullptr;
	if (!GetRandomUnit(unit, observation, unit_type)) {
		return false;
	}
	if (!unit->orders.empty()) {
		return false;
	}

	if (unit->build_progress != 1) {
		return false;
	}

	Actions()->UnitCommand(unit, ability_type_for_unit);
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

void BasicSc2Bot::AttackWithZerglings() {
	auto zerglings = Observation()->GetUnits([&](const Unit &unit) {
		return unit.unit_type == UNIT_TYPEID::ZERG_ZERGLING && unit.alliance == Unit::Alliance::Self;
	});

	// if enough zerglings
	if (zerglings.size() >= 12) {
		Point2D target = SeeEnemy();

		// if no enemies in sight
		if (target == Point2D(-1, -1)) {
			// If enemy structures have been found
			if (structure_target < structures.size()) {
				Point2D next_structure = structures[structure_target];

				// If at that structure and no structure is there, move to next target index
				// otherwise target that structure
				if (Distance2D(zerglings.front()->pos, next_structure) < 1.0) {
					++structure_target;
				} else {
					target = next_structure;
				}
			}
		}

		// if target has been found
		if (target != Point2D(-1, -1)) {
			for (auto &zergling : zerglings) {
				// Check if the Zergling is already moving to the target
				if (zergling->orders.empty() || zergling->orders.front().ability_id != ABILITY_ID::ATTACK) {
					// Issue the attack command to the target
					Actions()->UnitCommand(zergling, ABILITY_ID::ATTACK, target);
				}
			}
		}
	}
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

// Determine the damage of a unit
double BasicSc2Bot::FindDamage(const UnitTypeData unit_data) {
	double damage = 0;
	for (const auto &weapon : unit_data.weapons) {
		damage += weapon.damage_;
	}
	return damage;
}

// Determine if a unit is a structure
bool BasicSc2Bot::IsStructure(const UnitTypeData unit_data) {
	for (const auto &attribute : unit_data.attributes) {
		if (attribute == sc2::Attribute::Structure) {
			return true;
		}
	}
	return false;
}

// When an enemy is seen, return the most dangerous targets and add any structures to a vector
Point2D BasicSc2Bot::SeeEnemy() {
	Point2D best_target = Point2D(-1, -1);
	double most_damage = -INFINITY;

	// Look at all visable all Units
	for (const auto &enemy : Observation()->GetUnits(Unit::Alliance::Enemy)) {
		const auto &unit_info = Observation()->GetUnitTypeData().at(enemy->unit_type);
		if (IsStructure(unit_info)) {
			// Add Structure to
			structures.push_back(enemy->pos);
		} else {
			// Check for military unit or worker with most damage
			double unit_damage = FindDamage(unit_info);
			if (unit_damage > most_damage) {
				most_damage = unit_damage;
				best_target = enemy->pos;
			}
		}
	}

	return best_target;
}