#include "BasicSc2Bot.h"
#include "cpp-sc2/include/sc2api/sc2_common.h"
#include "cpp-sc2/include/sc2api/sc2_typeenums.h"
#include <iostream>
#include <ostream>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_unit_filters.h>

using namespace sc2;

void BasicSc2Bot::OnGameStart() { 
    const ObservationInterface* observation = Observation();
    possible_enemy_locations = observation->GetGameInfo().enemy_start_locations;

    // Force a drone to scout
    const Unit* scout_drone = FindAvailableDrone();
    if (scout_drone && !possible_enemy_locations.empty()) {
        Actions()->UnitCommand(scout_drone, ABILITY_ID::MOVE_MOVE, possible_enemy_locations[0]);
        current_scout_index = 1; // Prepare for next location
    }

    return;
}

void BasicSc2Bot::OnUnitIdle(const Unit* unit) {
    // If it's a drone that was scouting
    if (unit->unit_type == UNIT_TYPEID::ZERG_DRONE) {
        // Check if we have more locations to scout
        if (current_scout_index < possible_enemy_locations.size()) {
            Actions()->UnitCommand(unit, ABILITY_ID::MOVE_MOVE, possible_enemy_locations[current_scout_index]);
            current_scout_index++;
        } else {
            // Find our own base location to return to
            const Unit* townhall = FindNearestTownHall(unit->pos);
            if (townhall) {
                // First move to the base
                Actions()->UnitCommand(unit, ABILITY_ID::MOVE_MOVE, townhall->pos);
                
                // Then find a mineral field to mine
                const Unit* nearby_mineral = FindNearestMineralField(townhall->pos);
                if (nearby_mineral) {
                    // Queue the harvest gather command after moving
                    Actions()->UnitCommand(unit, ABILITY_ID::HARVEST_GATHER, nearby_mineral);
                }
            }
        }
    }
}

// New helper method to find nearest town hall
const Unit* BasicSc2Bot::FindNearestTownHall(const Point2D& start) {
    const ObservationInterface* observation = Observation();
    const Unit* nearest_townhall = nullptr;
    float min_distance = std::numeric_limits<float>::max();

    for (const auto& unit : observation->GetUnits()) {
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

// Helper method to find nearest mineral field
const Unit* BasicSc2Bot::FindNearestMineralField(const Point2D& start) {
    const ObservationInterface* observation = Observation();
    const Unit* nearest_mineral = nullptr;
    float min_distance = std::numeric_limits<float>::max();

    for (const auto& unit : observation->GetUnits()) {
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


void BasicSc2Bot::OnStep() {
	const ObservationInterface *observation = Observation();

    TryBuildZergling();
	
    static bool rush = false;
    // Find enemy base
    if (enemy_base_location.x == -1 && enemy_base_location.y == -1) {
        enemy_base_location = FindEnemyBase();
    } else if (CountUnitType(UNIT_TYPEID::ZERG_ZERGLING) >= 30) {
        AttackWithZerglings(enemy_base_location);
    } 
	//else{
	//	RetreatScouters();
	//}

	// Spawn more overlord if we are at the cap
	if (observation->GetFoodUsed() == observation->GetFoodCap()){
		TrySpawnOverlord();
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

	if (drone_count == 7 && observation->GetMinerals() >= 150 && observation->GetVespene() >= 100){
		TryBuildUnit(ABILITY_ID::MORPH_LAIR,UNIT_TYPEID::ZERG_HATCHERY);
		drone_count++;
	}

	if (drone_count == 8 && CountUnitType(UNIT_TYPEID::ZERG_LAIR) > 0){
		
		if (CountUnitType(UNIT_TYPEID::ZERG_INFESTATIONPIT) > 0){
			drone_count++;
		}else{
			TryBuildStructure(ABILITY_ID::BUILD_INFESTATIONPIT, UNIT_TYPEID::ZERG_DRONE);
		}
	}

	if (drone_count == 9){
		TryBuildUnit(ABILITY_ID::MORPH_HIVE, UNIT_TYPEID::ZERG_LAIR);
		if (CountUnitType(UNIT_TYPEID::ZERG_HIVE) > 0){
			drone_count++;
		}
	}

	// If not upgrades have been applied to any unit, upgrade zerglings
	 if (num_zergling_upgrades == 0) {
		std::vector<UpgradeID> completed_upgrades = observation->GetUpgrades();

		if (std::find(completed_upgrades.begin(), completed_upgrades.end(), UPGRADE_ID::ZERGLINGMOVEMENTSPEED) != completed_upgrades.end()) {
			num_zergling_upgrades++;
		} else {
			TryBuildUnit(ABILITY_ID::RESEARCH_ZERGLINGMETABOLICBOOST, UNIT_TYPEID::ZERG_SPAWNINGPOOL);
			
		}
    }else if (num_zergling_upgrades == 1) {
		TryBuildUnit(ABILITY_ID::RESEARCH_ZERGLINGADRENALGLANDS, UNIT_TYPEID::ZERG_SPAWNINGPOOL);
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


bool BasicSc2Bot::GetRandomUnit(const Unit*& unit_out, const ObservationInterface* observation, UnitTypeID unit_type) {
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
    const ObservationInterface* observation = Observation();

    // If a unit already is building a supply structure of this type, do nothing.
    Units units = observation->GetUnits(Unit::Alliance::Self);
    for (const auto& unit : units) {
        for (const auto& order : unit->orders) {
            if (order.ability_id == ability_type_for_structure) {
                return false;
            }
        }
    }

    // Just try a random location near the unit.
    const Unit* unit = nullptr;
    if (!GetRandomUnit(unit, observation, unit_type)){
        return false;
	}

    float rx = GetRandomScalar();
    float ry = GetRandomScalar();

    Actions()->UnitCommand(unit, ability_type_for_structure, unit->pos + Point2D(rx, ry) * 9.0f);
    return true;
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

bool BasicSc2Bot::TryBuildUnit(AbilityID ability_type_for_unit, UnitTypeID unit_type) {
    const ObservationInterface* observation = Observation();

    //If we are at supply cap, don't build anymore units, unless its an overlord.
    if (observation->GetFoodUsed() >= observation->GetFoodCap() && ability_type_for_unit != ABILITY_ID::TRAIN_OVERLORD) {
        return false;
    }
    const Unit* unit = nullptr;
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
        if (unit->unit_type == UNIT_TYPEID::ZERG_DRONE) {
            // Force the first drone to stop mining and scout
            Actions()->UnitCommand(unit, ABILITY_ID::STOP);
            return unit;
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

// Scout Drone Management
void BasicSc2Bot::RetreatScouters() {
	Units scout_drones = Observation()->GetUnits(Unit::Alliance::Self, [&](const Unit& unit) {
		return unit.unit_type == UNIT_TYPEID::ZERG_DRONE && 
			   !unit.orders.empty() && 
			   unit.orders[0].ability_id == ABILITY_ID::MOVE_MOVE;
	});

    for (const auto& scout_drone : scout_drones) {
        // If scout drone is in danger or spotted, retreat to start location
        Point2D retreat_position = Observation()->GetStartLocation();
        Actions()->UnitCommand(scout_drone, ABILITY_ID::MOVE_MOVE, retreat_position);
    }
}

Point2D BasicSc2Bot::FindEnemyBase() {
    // Existing enemy base finding logic
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