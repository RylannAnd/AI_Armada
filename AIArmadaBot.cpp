#include "AIArmadaBot.h"
#include "cpp-sc2/include/sc2api/sc2_common.h"
#include "cpp-sc2/include/sc2api/sc2_typeenums.h"
#include "cpp-sc2/include/sc2lib/sc2_search.h"
#include <cmath>
#include <iostream>
#include <ostream>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_map_info.h>
#include <sc2api/sc2_unit_filters.h>

using namespace sc2;

// ./BasicSc2Bot.exe -c -a zerg -d Hard -m CactusValleyLE.SC2Map
void AIArmadaBot::OnGameStart() {
	return;
}

void AIArmadaBot::OnUnitIdle(const Unit *unit) {
	// If it's a drone that was scouting
	if (unit->unit_type == UNIT_TYPEID::ZERG_DRONE) {
		// Check if we have more locations to scout
		if (structures.empty()) {
			if (current_scout_index >= possible_enemy_locations.size()) {
				current_scout_index = 0;
			}
			action->UnitCommand(unit, ABILITY_ID::MOVE_MOVE, possible_enemy_locations[current_scout_index]);
			current_scout_index++;
		} else {
			// Find our own base location to return to
			const Unit *townhall = FindNearestTownHall(unit->pos);
			if (townhall) {
				// First move to the base
				action->UnitCommand(unit, ABILITY_ID::MOVE_MOVE, townhall->pos);

				// Then find a mineral field to mine
				const Unit *nearby_mineral = FindNearestMineralField(townhall->pos);
				if (nearby_mineral) {
					// Queue the harvest gather command after moving
					action->UnitCommand(unit, ABILITY_ID::HARVEST_GATHER, nearby_mineral);
				}
			}
		}
	}
}

void AIArmadaBot::OnStep() {
	static int step_counter = 0;
	++step_counter;
	// Wait for 10 frames
	if (step_counter < 10) {
		return;
	}

	// Initilize Values
	static int initilized = 0;
	if (initilized <= 0) {
		observation = Observation();
		query = Query();
		action = Actions();

		++initilized;
		return;
	} else if (initilized <= 1) {
		start_location = observation->GetStartLocation();
		possible_enemy_locations = observation->GetGameInfo().enemy_start_locations;
		expansion_locations = sc2::search::CalculateExpansionLocations(observation, query);
		expansion_locations_seen = {Point2D(0, 0)};

		// Start Scouting
		const Unit *scout_drone = FindAvailableDrone();
		if (scout_drone && !possible_enemy_locations.empty()) {
			action->UnitCommand(scout_drone, ABILITY_ID::MOVE_MOVE, possible_enemy_locations[0]);
			current_scout_index = 1; // Prepare for next location
		}
			initilized = true;
		
		++initilized;
		return;
	}

	// Spawn overlords as needed
	if (observation->GetFoodCap() <= observation->GetFoodUsed() + 8) {
		TrySpawnOverlord();
	}

	// Spawn drones as neeeded with initial cap being 20
	static int drone_cap = 20;
	if (CountUnitType(UNIT_TYPEID::ZERG_DRONE) < drone_cap) {
		TryBuildDrone();
	}

	// SETUP PHASE =================================================================================================
	static bool spawn_pool = true;
	static bool create_extractor = true;
	static bool expand = true;
	static bool zerglings_upgraded = false;
	static bool unassign_extractor_workers = true;

	// Every extra drone spawned at setup phase will go through building core buildings
	if (observation->GetFoodUsed() >= 17 && expand) {
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

	// ATTACKING PHASE =================================================================================================
	TryBuildZergling();
	AttackWithZerglings();

	if (CountUnitType(UNIT_TYPEID::ZERG_HATCHERY) > 1) { // execute attacks and upgrades after expanding

		// Make Queens
		if (CountUnitType(UNIT_TYPEID::ZERG_QUEEN) < 2 && observation->GetMinerals() >= 150) {
			TryBuildQueen();
		}

		// extractor workers
		if (CountUnitType(UNIT_TYPEID::ZERG_EXTRACTOR) > 0 && !zerglings_upgraded) {
			AssignExtractorWorkers();
		}

		// Upgrade zerling abilities
		if (!zerglings_upgraded) {
			std::vector<UpgradeID> completed_upgrades = observation->GetUpgrades();

			if (std::find(completed_upgrades.begin(), completed_upgrades.end(), UPGRADE_ID::ZERGLINGMOVEMENTSPEED) != completed_upgrades.end()) {
				zerglings_upgraded = true;
			} else {
				TryBuildUnit(ABILITY_ID::RESEARCH_ZERGLINGMETABOLICBOOST, UNIT_TYPEID::ZERG_SPAWNINGPOOL);
			}
		} 

		if (zerglings_upgraded && unassign_extractor_workers){
			Units extractors = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_EXTRACTOR));
			const Unit *extractor = extractors[0];
			if (extractor->assigned_harvesters >= 1){
				UnAssignExtractorWorkers();	
			}else{
				unassign_extractor_workers = false;
			}
		}
	}

	// Do Injections for extra larvae
	if (CountUnitType(UNIT_TYPEID::ZERG_QUEEN) > 0) {
		TryInject();
	}	
}

// BUILD CORE BUILDINGS
// =================================================================================================
bool AIArmadaBot::TryBuildExtractor() {
	if (observation->GetMinerals() > 25) {
		// Find nearest Vespene Geyser
		const Unit *geyser = FindNearestVespeneGeyser(start_location);
		const Unit *builder_drone = FindAvailableDrone();
		if (geyser && builder_drone) {
			action->UnitCommand(builder_drone, ABILITY_ID::BUILD_EXTRACTOR, geyser);
			return true;
		}
	}
	return false;
}

bool AIArmadaBot::TryBuildSpawningPool() {
	// Check if we already have a Spawning Pool
	if (CountUnitType(UNIT_TYPEID::ZERG_SPAWNINGPOOL) > 0) {
		return false;
	}

	// Check if we have enough resources
	if (observation->GetMinerals() >= 200) { // Spawning Pool costs 200 minerals
		const Unit *builder_drone = FindAvailableDrone();
		if (builder_drone) {
			// Define a list of potential nearby build locations
			std::vector<Point2D> build_locations = {
				start_location + Point2D(5, 5),  // Bottom Right
				start_location + Point2D(-5, 5), // Bottom Left
				start_location + Point2D(5, -5), // Top Right
				start_location + Point2D(-5, -5) // Top Left
			};

			for (const auto &location : build_locations) {
				if (query->Placement(ABILITY_ID::BUILD_SPAWNINGPOOL, location)) {
					action->UnitCommand(builder_drone, ABILITY_ID::BUILD_SPAWNINGPOOL, location);
					return true;
				}
			}
		}
	}
	return false;
}

bool AIArmadaBot::TryBuildHatcheryInNatural() {
	if (observation->GetMinerals() < 300) { // hatchery cost
		return false;
	}

	Point2D natural_expansion_pos = FindNaturalExpansionLocation(start_location, false);

	// get  available drone to build the hatchery
	const Unit *builder_drone = FindAvailableDrone();
	if (!builder_drone) {
		return false;
	}

	// verify that the location is buildable
	if (query->Placement(ABILITY_ID::BUILD_HATCHERY, natural_expansion_pos, builder_drone)) {
		action->UnitCommand(builder_drone, ABILITY_ID::BUILD_HATCHERY, natural_expansion_pos);
		return true;
	}

	return false; // unable to find a suitable location
}
// =================================================================================================

// BUILD CORE UNITS
// =================================================================================================
bool AIArmadaBot::TryBuildDrone() {
	// Build a drone when we have two overlord
	if (observation->GetMinerals() >= 50 && observation->GetFoodUsed() <= 20 && CountUnitType(UNIT_TYPEID::ZERG_DRONE) < 20) {
		// Find a larva to morph into a Drone.
		const Unit *larva = FindNearestLarva();
		if (larva) {
			action->UnitCommand(larva, ABILITY_ID::TRAIN_DRONE);
			return true;
		}
	}
	return false;
}

bool AIArmadaBot::TrySpawnOverlord() {
	if (observation->GetMinerals() >= 100) { // Overlord costs 100 minerals
		// Find a larva to morph into an Overlord.
		const Unit *larva = FindNearestLarva();
		if (larva) {
			action->UnitCommand(larva, ABILITY_ID::TRAIN_OVERLORD);
			return true;
		}
	}
	return false;
}

bool AIArmadaBot::TryBuildQueen() {
	// Check if enough minerals are available for a Queen.
	if (observation->GetMinerals() >= 150) {
		for (const auto &unit : observation->GetUnits(Unit::Alliance::Self)) {
			if (unit->unit_type == UNIT_TYPEID::ZERG_HATCHERY) {

				if (!unit->orders.empty()) {
					action->UnitCommand(unit, ABILITY_ID::STOP);
				}

				action->UnitCommand(unit, ABILITY_ID::TRAIN_QUEEN);
				return true;
			}
		}
	}
	return false; // Not enough minerals or no eligible Hatchery/Lair/Hive found.
}

bool AIArmadaBot::TryBuildZergling() {
	if (CountUnitType(UNIT_TYPEID::ZERG_SPAWNINGPOOL) && observation->GetMinerals() >= 50) {
		const Unit *larva = FindNearestLarva();
		if (larva) {
			action->UnitCommand(larva, ABILITY_ID::TRAIN_ZERGLING);
			return true;
		}
	}
	return false;
}

bool AIArmadaBot::TryBuildUnit(AbilityID ability_type_for_unit, UnitTypeID unit_type) {
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

	action->UnitCommand(unit, ability_type_for_unit);
	return true;
}
// =================================================================================================

// FIND UNITS
// =================================================================================================
const Unit *AIArmadaBot::FindNearestLarva() {
	Units units = observation->GetUnits(Unit::Alliance::Self);
	for (const auto &unit : units) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_LARVA) {
			return unit;
		}
	}
	return nullptr;
}

const Unit *AIArmadaBot::FindAvailableDrone() {
	Units units = observation->GetUnits(Unit::Alliance::Self);

	for (const auto &unit : units) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_DRONE && !unit->orders.empty()) {
			// Ensure the drone's last order is to gather resources
			const auto &last_order = unit->orders.back();
			if (last_order.ability_id == ABILITY_ID::HARVEST_GATHER) {
				action->UnitCommand(unit, ABILITY_ID::STOP);
				return unit;
			}
		}
	}
	return nullptr;
}

const Unit *AIArmadaBot::FindNearestMineralField(const Point2D &start) {
	const Unit *nearest_mineral = nullptr;
	float min_distance = std::numeric_limits<float>::max();

	for (const auto &unit : observation->GetUnits()) {
		if (unit->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD) {
			float distance = DistanceSquared2D(unit->pos, start);
			if (distance < min_distance) {
				min_distance = distance;
				nearest_mineral = unit;
			}
		}
	}
	return nearest_mineral;
}

const Unit *AIArmadaBot::FindNearestTownHall(const Point2D &start) {
	const Unit *nearest_townhall = nullptr;
	float min_distance = std::numeric_limits<float>::max();

	for (const auto &unit : observation->GetUnits()) {
		// Check for Zerg town halls (Hatchery, Lair, Hive)
		if (unit->unit_type == UNIT_TYPEID::ZERG_HATCHERY ||
			unit->unit_type == UNIT_TYPEID::ZERG_LAIR ||
			unit->unit_type == UNIT_TYPEID::ZERG_HIVE) {
			float distance = DistanceSquared2D(unit->pos, start);
			if (distance < min_distance) {
				min_distance = distance;
				nearest_townhall = unit;
			}
		}
	}
	return nearest_townhall;
}

int AIArmadaBot::CountUnitType(UNIT_TYPEID unit_type) {
	Units units = observation->GetUnits(Unit::Alliance::Self);
	int count = 0;
	for (const auto &unit : units) {
		if (unit->unit_type == unit_type) {
			count++;
		}
	}
	return count;
}

bool AIArmadaBot::GetRandomUnit(const Unit *&unit_out, const ObservationInterface *observation, UnitTypeID unit_type) {
	Units my_units = observation->GetUnits(Unit::Alliance::Self);
	for (const auto unit : my_units) {
		if (unit->unit_type == unit_type) {
			unit_out = unit;
			return true;
		}
	}
	return false;
}
// =================================================================================================

// FIND UNIT DATA
// =================================================================================================
double AIArmadaBot::FindDamage(const UnitTypeData unit_data) {
	double damage = 0;
	for (const auto &weapon : unit_data.weapons) {
		damage += weapon.damage_;
	}
	return damage;
}

bool AIArmadaBot::IsStructure(const UnitTypeData unit_data) {
	for (const auto &attribute : unit_data.attributes) {
		if (attribute == sc2::Attribute::Structure) {
			return true;
		}
	}
	return false;
}
// =================================================================================================

// FIND LOCATIONS
// =================================================================================================
const Unit *AIArmadaBot::FindNearestVespeneGeyser(const Point2D &start) {
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

Point2D AIArmadaBot::FindNaturalExpansionLocation(const Point2D &location, bool not_seen) {
	// If all expansion locations have been visited, clear list and start again
	if (expansion_locations_seen.size() >= expansion_locations.size()) {
		expansion_locations_seen = {Point2D(0, 0)};
	}

	Point3D min = Point3D(1000, 1000, 1000);
	for (auto it : expansion_locations) {
		std::cout << it.x << ", " << it.y << std::endl;
		if (abs(location.x - it.x) + abs(location.y - it.y) < abs(location.x - min.x) + abs(location.y - min.y)) {
			if ((it.x != 0 || it.y != 0) && (!not_seen || std::find(expansion_locations_seen.begin(), expansion_locations_seen.end(), it) == expansion_locations_seen.end())){
				min = it;
			}
		}
	}
	return min;
}
// =================================================================================================

// ATTACKING / SCOUTING
// =================================================================================================
// When an enemy is seen, return the most dangerous targets and add any structures to a vector
Point2D AIArmadaBot::SeeEnemy() {
	Point2D best_target = Point2D(-1, -1);
	double most_damage = -INFINITY;

	// Look at all visable all Units
	for (const auto &enemy : observation->GetUnits(Unit::Alliance::Enemy)) {
		const auto &unit_info = observation->GetUnitTypeData().at(enemy->unit_type);
		if (IsStructure(unit_info) && std::find(structures.begin(), structures.end(), enemy->pos) == structures.end()) {
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

void AIArmadaBot::AttackWithZerglings() {
	Point2D target = SeeEnemy();

	auto zerglings = observation->GetUnits([&](const Unit &unit) {
		return unit.unit_type == UNIT_TYPEID::ZERG_ZERGLING && unit.alliance == Unit::Alliance::Self;
	});

	// if enough zerglings
	if (zerglings.size() >= 12) {
		// if no enemies in sight
		if (target == Point2D(-1, -1)) {
			// If enemy structures have been found, else go to next natural resource location
			if (structure_target < structures.size()) {
				Point2D next_structure = structures[structure_target];

				// If at that structure and no structure is there, move to next target index
				// otherwise target that structure
				if (Distance2D(zerglings.front()->pos, next_structure) < 1.0) {
					++structure_target;
				} else {
					target = next_structure;
				}
			} else {
				Point2D next_structure = FindNaturalExpansionLocation(zerglings.front()->pos, true);

				// If at that natural location and no enemies, move to next target index
				// otherwise target that location
				if (Distance2D(zerglings.front()->pos, next_structure) < 5.0) {
					expansion_locations_seen.push_back(next_structure);
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
					action->UnitCommand(zergling, ABILITY_ID::ATTACK, target);
				}
			}
		}
	} else { // Not enough zerglings, meet at base
		for (auto &zergling : zerglings) {
				// Check if the Zergling is already moving to the target
				if (zergling->orders.empty() || zergling->orders.front().ability_id != ABILITY_ID::ATTACK) {
					// Issue the attack command to the target
					action->UnitCommand(zergling, ABILITY_ID::MOVE_MOVE, start_location);
				}
			}
	}
}
// =================================================================================================

// UNIT ACTIONS
// =================================================================================================
void AIArmadaBot::TryInject() {
    // Iterate through all Hatcheries
    for (const auto &hatchery : observation->GetUnits(Unit::Alliance::Self)) {
        if (hatchery->unit_type == UNIT_TYPEID::ZERG_HATCHERY) {

            // Check if the Hatchery is eligible for an injection
            bool alreadyInjected = false;
            for (const auto &order : hatchery->orders) {
                if (order.ability_id == ABILITY_ID::EFFECT_INJECTLARVA) {
                    alreadyInjected = true;
                    break;
                }
            }

            if (!alreadyInjected) {
                // Iterate through Queens to find one that can inject
                for (const auto &queen : observation->GetUnits(Unit::Alliance::Self)) {
                    if (queen->unit_type == UNIT_TYPEID::ZERG_QUEEN &&
                        queen->energy >= 25 && 
						queen->orders.empty()) { // Queens need at least 25 energy to inject
                        // Command the Queen to inject the Hatchery
                        action->UnitCommand(queen, ABILITY_ID::EFFECT_INJECTLARVA, hatchery);
                        break; // Move to the next Hatchery after assigning a Queen
                    }
                }
            }
        }
    }
}

void AIArmadaBot::AssignExtractorWorkers() {
	Units extractors = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_EXTRACTOR));
	const Unit *extractor = extractors[0];
	static int assigned = 0;

	if (assigned < 3) {
		const Unit *unit = FindAvailableDrone();
		action->UnitCommand(unit, ABILITY_ID::HARVEST_GATHER, extractor);
		++assigned;
	}
}

void AIArmadaBot::UnAssignExtractorWorkers() {
    Units drones = observation->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::ZERG_DRONE));
	Units hatchery = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_HATCHERY));

    for (const auto& drone : drones) {
        if (!drone->orders.empty()) {
            const UnitOrder& current_order = drone->orders.front();
			// Get the resource being gathered (minerals or gas)
			const Unit* target = observation->GetUnit(current_order.target_unit_tag);
			if (target && target->unit_type == UNIT_TYPEID::ZERG_EXTRACTOR) {
								action->UnitCommand(drone, ABILITY_ID::STOP);
								action->UnitCommand(drone, ABILITY_ID::MOVE_MOVE, hatchery[1]->pos);
								action->UnitCommand(drone, ABILITY_ID::HARVEST_GATHER, observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_HATCHERY))[1]);
			}
        }
	}
}
// =================================================================================================
