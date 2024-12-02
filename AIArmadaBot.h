#ifndef BASIC_SC2_BOT_H_
#define BASIC_SC2_BOT_H_

#include "sc2api/sc2_api.h"
#include "sc2api/sc2_args.h"
#include "sc2lib/sc2_lib.h"
#include "sc2utils/sc2_manage_process.h"
#include "sc2utils/sc2_arg_parser.h"
#include <sc2api/sc2_unit_filters.h>

using namespace sc2;
class AIArmadaBot : public sc2::Agent {
public:
	virtual void OnGameStart();
    virtual void OnUnitIdle(const Unit* unit);
	virtual void OnStep();

private:
    // BUILD CORE BUILDINGS
    bool TryBuildExtractor();
    bool TryBuildSpawningPool();
    bool TryBuildHatcheryInNatural();

    // BUILD CORE UNITS
    bool TryBuildDrone();
    bool TrySpawnOverlord();
    bool TryBuildQueen();
    bool TryBuildZergling();
    bool TryBuildUnit(AbilityID ability_type_for_unit, UnitTypeID unit_type);

    // FIND UNITS
    const Unit* FindNearestLarva();
    const Unit* FindAvailableDrone();
    const Unit* FindNearestMineralField(const Point2D& start);
    const Unit* FindNearestTownHall(const Point2D& start);
    int CountUnitType(UNIT_TYPEID unit_type);
    bool GetRandomUnit(const Unit*& unit_out, const ObservationInterface* observation, UnitTypeID unit_type);

    // FIND UNIT DATA
    double FindDamage(const UnitTypeData unit_data);
    bool IsStructure(const UnitTypeData unit_data);

    // FIND LOCATIONS
    const Unit* FindNearestVespeneGeyser(const Point2D& start);
    Point2D FindNaturalExpansionLocation(const Point2D &location, bool not_seen);

    // ATTACKING / SCOUTING
    Point2D SeeEnemy();
    void AttackWithZerglings();

    // UNIT ACTIONS
    void TryInject();
    void AssignExtractorWorkers();
    void UnAssignExtractorWorkers();
    
    // DATA
    int structure_target = 0;
    size_t current_scout_index = 0;

    std::vector<Point2D> structures;
    std::vector<Point2D> expansion_locations_seen;

    // Need to be Initilized in OnStart()
    const ObservationInterface* observation;
    ActionInterface* action;
    QueryInterface* query;
    Point2D start_location;
    std::vector<Point2D> possible_enemy_locations;
    std::vector<Point3D> expansion_locations;
};

#endif