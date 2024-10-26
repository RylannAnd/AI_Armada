#include "BasicSc2Bot.h"
#include "cpp-sc2/include/sc2api/sc2_common.h"
#include "cpp-sc2/include/sc2api/sc2_typeenums.h"
#include <sc2api/sc2_unit_filters.h>
#include <sc2api/sc2_interfaces.h>

using namespace sc2;


void BasicSc2Bot::OnGameStart() { return; }

void BasicSc2Bot::OnStep() {
  TryBuildDrone();
  TryBuildHatchery();
  TrySpawnOverlord();

    // start building zerglings if we have enough drones
    if (CountUnitType(UNIT_TYPEID::ZERG_DRONE) >= 12) {
        TryBuildZergling();
    }

    // trigger attack if we have enough zerglings
    if (CountUnitType(UNIT_TYPEID::ZERG_ZERGLING) >= 20) {
        AttackWithZerglings();
    }
}

//  void BasicSc2Bot::OnUnitIdle(const Unit* unit) {
//     switch (unit->unit_type.ToType()) {
//         case UNIT_TYPEID::ZERG_DRONE: {
//             // This is where the newly spawned Drone would be detected as idle.
//             // Override its default behavior to keep it idle.
//             Actions()->UnitCommand(unit, ABILITY_ID::STOP);
//             break;
//         }
//         default:
//             break;
//     }
// }
bool BasicSc2Bot::TrySpawnOverlord() {
    const ObservationInterface* observation = Observation();

    // Check if the supply is close to the cap
    if (observation->GetFoodUsed() >= observation->GetFoodCap() - 2 &&
        observation->GetMinerals() >= 100) { // Overlord costs 100 minerals
        // Find a larva to morph into an Overlord.
        const Unit* larva = FindNearestLarva();
        if (larva) {
            Actions()->UnitCommand(larva, ABILITY_ID::TRAIN_OVERLORD);
            return true;
        }
    }
    return false;
}

bool BasicSc2Bot::TryBuildDrone() {
        const ObservationInterface* observation = Observation();
        int drone_count = CountUnitType(UNIT_TYPEID::ZERG_DRONE);
        int hatchery_count = CountUnitType(UNIT_TYPEID::ZERG_HATCHERY);

        // Ideal worker count per Hatchery is 16 on minerals, plus gas workers if applicable.
        int ideal_drone_count = hatchery_count * 16;

        // Check if there are enough resources and if we need more drones.
        if (observation->GetMinerals() >= 50 && drone_count < ideal_drone_count) {
            // Find a larva to morph into a Drone.
            const Unit* larva = FindNearestLarva();
            if (larva) {
                Actions()->UnitCommand(larva, ABILITY_ID::TRAIN_DRONE);
                // Actions()->UnitCommand(unit, ABILITY_ID::STOP);
                return true;
            }
        }
        return false;
    }

    bool BasicSc2Bot::TryBuildHatchery() {
        const ObservationInterface* observation = Observation();
        int drone_count = CountUnitType(UNIT_TYPEID::ZERG_DRONE);
        int hatchery_count = CountUnitType(UNIT_TYPEID::ZERG_HATCHERY);

        // Ideal drone count threshold before building a new Hatchery
        int threshold_drone_count = hatchery_count * 16;

        // Check if we need more Hatcheries for increased capacity.
        if (drone_count >= threshold_drone_count && observation->GetMinerals() >= 300) {
       
            // Find a suitable Drone to build the Hatchery.
            const Unit* builder_drone = FindAvailableDrone();
            if (builder_drone) {
                std::cout << drone_count << " building hatchery " << threshold_drone_count << std::endl;
                const GameInfo& game_info = Observation()->GetGameInfo();

                Point2D loc = FindRandomLocation(game_info);

                Actions()->UnitCommand(builder_drone, ABILITY_ID::BUILD_HATCHERY,
                    loc);
                return true;
            }
        }
        return false;
    }

    const Unit* BasicSc2Bot::FindNearestLarva() {
        Units units = Observation()->GetUnits(Unit::Alliance::Self);
        for (const auto& unit : units) {
            if (unit->unit_type == UNIT_TYPEID::ZERG_LARVA) {
                return unit;
            }
        }
        return nullptr;
    }

    const Unit* BasicSc2Bot::FindAvailableDrone() {
      const ObservationInterface* observation = Observation();
        Units units = observation->GetUnits(Unit::Alliance::Self);
       
       
        for (const auto& unit : units) {
           const auto& last_order = unit->orders.back();
            if (unit->unit_type == UNIT_TYPEID::ZERG_DRONE && last_order.ability_id == ABILITY_ID::HARVEST_GATHER ) {
                Actions()->UnitCommand(unit, ABILITY_ID::STOP); 
                return unit;
            }
        }
        return nullptr;
    }

    int BasicSc2Bot::CountUnitType(UNIT_TYPEID unit_type) {
        Units units = Observation()->GetUnits(Unit::Alliance::Self);
        int count = 0;
        for (const auto& unit : units) {
            if (unit->unit_type == unit_type) {
                count++;
            }
        }
        return count;
    }

// spawning zerglings for atttack
bool BasicSc2Bot::TryBuildZergling() {
    const ObservationInterface* observation = Observation();
    if (observation->GetMinerals() >= 50) {
        const Unit* larva = FindNearestLarva();
        if (larva) {
            Actions()->UnitCommand(larva, ABILITY_ID::TRAIN_ZERGLING);
            return true;
        }
    }
    return false;
}

for
