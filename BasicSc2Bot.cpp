#include "BasicSc2Bot.h"
#include "cpp-sc2/include/sc2api/sc2_common.h"
#include "cpp-sc2/include/sc2api/sc2_typeenums.h"
#include <iostream>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_unit_filters.h>

using namespace sc2;

void BasicSc2Bot::OnGameStart() { 
	const ObservationInterface* observation = Observation();
    possible_enemy_locations = observation->GetGameInfo().enemy_start_locations;

	return;
}

void BasicSc2Bot::OnUnitIdle(const Unit* unit){
	if (unit->unit_type == UNIT_TYPEID::ZERG_OVERLORD) {
		// Move the Overlord to the next possible enemy location.
		if (enemy_base_location.x == -1 && enemy_base_location.y == -1){
			Actions()->UnitCommand(unit, ABILITY_ID::MOVE_MOVE, possible_enemy_locations[current_scout_index]);
			current_scout_index = ++current_scout_index % possible_enemy_locations.size();
		}
	}
}


void BasicSc2Bot::OnStep() {
	const ObservationInterface *observation = Observation();

    TryBuildZergling();

    static bool rush = false;
    // Find enemy base
    if (enemy_base_location.x == -1 && enemy_base_location.y == -1) {
        enemy_base_location = FindEnemyBase();
    } else if (CountUnitType(UNIT_TYPEID::ZERG_ZERGLING) >= 10) {
        AttackWithZerglings(enemy_base_location);
    } else{
		RetreatScouters();
	}

	// Spawn more overlord if we are at the cap
	if (observation->GetFoodUsed() == observation->GetFoodCap()){
		TrySpawnOverlord();
	}

	TryBuildDrone();

	TryBuildLair();
	// TryBuildInfestationPit();
	// TryBuildHive();

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

	// Assign extra drones 3-5 to the extractor
	if (drone_count >= 3 && drone_count <= 6 && observation->GetFoodUsed() == 20){
		Units extractors = Observation()->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_EXTRACTOR));
		const Unit* extractor = extractors[0];
		if (extractor->assigned_harvesters <= 4){
			const Unit* unit = FindAvailableDrone();
			Actions()->UnitCommand(unit, ABILITY_ID::HARVEST_GATHER, extractor);
			drone_count++;
		}
	}

	// If not upgrades have been applied to any unit, upgrade zerglings
	 if (num_zergling_upgrades == 0) {
		UpgradeZerglings();
		num_zergling_upgrades++;
    }else if (num_zergling_upgrades == 1) {
		UpgradeZerglingsAdrenalGlands();
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

bool BasicSc2Bot::TryBuildLair() {
	const ObservationInterface *observation = Observation();

	// Check if we already have a Lair
	if (CountUnitType(UNIT_TYPEID::ZERG_LAIR) > 0) {
		return true;
	}

	// Find the Hatchery unit (a prerequisite for building a Lair)
	const Unit *hatchery = nullptr;
	for (const auto &unit : observation->GetUnits()) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_HATCHERY && unit->alliance == Unit::Alliance::Self) {
			hatchery = unit;
			break;
		}
	}

	// Check if we have enough resources to build the Lair (150 minerals, 100 vespene gas)
	if (observation->GetMinerals() >= 150 && observation->GetVespene() >= 100 && hatchery != nullptr) {
		std::cout << "Morphing hatchery into lair" << std::endl;
		Actions()->UnitCommand(hatchery, ABILITY_ID::MORPH_LAIR);
	}

	return false;
}

bool BasicSc2Bot::TryBuildInfestationPit() {
    const ObservationInterface *observation = Observation();

    // Check if we already have an Infestation Pit
    if (CountUnitType(UNIT_TYPEID::ZERG_INFESTATIONPIT) > 0) {
        return true;
    }

    const Unit *lair = nullptr;
    for (const auto &unit : observation->GetUnits()) {
        if (unit->unit_type == UNIT_TYPEID::ZERG_LAIR && unit->alliance == Unit::Alliance::Self) {
            lair = unit;
            break;
        }
    }

    // Check if we have enough resources
    if (observation->GetMinerals() >= 100 && observation->GetVespene() >= 100 && lair != nullptr) {
        const Unit *builder_drone = FindAvailableDrone();
        if (builder_drone) {
			std::cout << "Building Infestation Pit near Lair" << std::endl;
            Actions()->UnitCommand(builder_drone, ABILITY_ID::BUILD_INFESTATIONPIT,builder_drone->pos);
			building_pit = true;
        }
    }
    return false;
}


bool BasicSc2Bot::TryBuildHive() {
	const ObservationInterface *observation = Observation();

	// Check if we already have a Hive
	if (CountUnitType(UNIT_TYPEID::ZERG_HIVE) > 0) {
		return true;
	}

	// Find the Lair unit (a prerequisite for building a Hive)
	const Unit *lair = nullptr;
	for (const auto &unit : observation->GetUnits()) {
		if (unit->unit_type == UNIT_TYPEID::ZERG_LAIR && unit->alliance == Unit::Alliance::Self) {
			lair = unit;
			break;
		}
	}

	// Check if we have enough resources to build the Hive (300 minerals, 200 vespene gas)
	if (observation->GetMinerals() >= 300 && observation->GetVespene() >= 200 && lair != nullptr) {
		
		std::cout << "Morphing Lair into hive" << std::endl;
		Actions()->UnitCommand(lair, ABILITY_ID::MORPH_HIVE);
	
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
			const Unit* target_unit = observation->GetUnit(last_order.target_unit_tag);
			if (last_order.ability_id == ABILITY_ID::HARVEST_GATHER && target_unit->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD) {
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

void BasicSc2Bot::AttackWithZerglings(Point2D target) {
    auto zerglings = Observation()->GetUnits([&](const Unit& unit) {
            return unit.unit_type == UNIT_TYPEID::ZERG_ZERGLING && unit.alliance == Unit::Alliance::Self;
    });

    if (target != Point2D(0, 0)) {
        for (const auto& zergling : zerglings) {
            Actions()->UnitCommand(zergling, ABILITY_ID::ATTACK, target);
        }
    }
}

// For later scouting use
Point2D BasicSc2Bot::FindEnemyBase() {
    for (const auto& enemy_struct : Observation()->GetUnits(Unit::Alliance::Enemy)) {

        if (enemy_struct->unit_type == UNIT_TYPEID::ZERG_HATCHERY || 
            enemy_struct->unit_type == UNIT_TYPEID::PROTOSS_NEXUS || 
            enemy_struct->unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTER) {
            return enemy_struct->pos;
        }

		// Check for enemy units (combat units like Zerglings, Marines, etc.)
    	for (const auto& enemy_unit : Observation()->GetUnits(Unit::Alliance::Enemy)) {
        if (enemy_unit->unit_type == UNIT_TYPEID::ZERG_ZERGLING || 
            enemy_unit->unit_type == UNIT_TYPEID::TERRAN_MARINE || 
            enemy_unit->unit_type == UNIT_TYPEID::PROTOSS_ZEALOT) {
            return enemy_unit->pos; // Found an enemy unit
        }
    }
    }
    return Point2D(-1, -1);
}

// Method to detect if an Overlord sees enemy units/structures and commands retreat.
void BasicSc2Bot::RetreatScouters() {
	auto overlords = Observation()->GetUnits([&](const Unit& unit) {
            return unit.unit_type == UNIT_TYPEID::ZERG_OVERLORD && unit.alliance == Unit::Alliance::Self;
    });
	int count = 1;
	for (const auto& overlord : overlords) {
		const ObservationInterface* observation = Observation();

		// Command the Overlord to retreat to a safe location.
		Point2D retreat_position = observation->GetStartLocation();

		Actions()->UnitCommand(overlord, ABILITY_ID::MOVE_MOVE, retreat_position);

	}
}


// UPGRADING
// ======================================================================================================================
void BasicSc2Bot::UpgradeZerglings(){
	const ObservationInterface *observation = Observation();
	Units spawning_pools = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_SPAWNINGPOOL));
	
	if (!spawning_pools.empty() && observation->GetMinerals() >= 100 && observation->GetVespene() >= 100){
   		Actions()->UnitCommand(spawning_pools[0], ABILITY_ID::RESEARCH_ZERGLINGMETABOLICBOOST);
	}
}

void BasicSc2Bot::UpgradeZerglingsAdrenalGlands(){
    const ObservationInterface *observation = Observation();

    // Check if you have a Spawning Pool
    Units spawning_pools = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_SPAWNINGPOOL));

    // Check if you have a Hive
    Units hives = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_HIVE));

    // If you have a Spawning Pool, a Hive, and enough resources
    if (!spawning_pools.empty() && !hives.empty() &&
        observation->GetMinerals() >= 150 && observation->GetVespene() >= 150) {
        
        // Perform the upgrade for Adrenal Glands
        Actions()->UnitCommand(spawning_pools[0], ABILITY_ID::RESEARCH_ZERGLINGADRENALGLANDS);
    }
}
