#ifndef BASIC_SC2_BOT_H_
#define BASIC_SC2_BOT_H_

#include "sc2api/sc2_api.h"
#include "sc2api/sc2_args.h"
#include "sc2lib/sc2_lib.h"
#include "sc2utils/sc2_manage_process.h"
#include "sc2utils/sc2_arg_parser.h"
#include <sc2api/sc2_unit_filters.h>

using namespace sc2;
class BasicSc2Bot : public sc2::Agent {
public:
	virtual void OnGameStart();
	virtual void OnStep();
    // virtual void OnUnitIdle(const Unit* unit);

private:
    bool TryBuildDrone();
    bool TrySpawnOverlord();
    bool TryBuildHatcheryInNatural();
    bool TryBuildSpawningPool();
    bool TryBuildExtractor();
    bool TryBuildQueen();
    bool TryInject();
    Point2D FindNaturalExpansionLocation(const Point2D &main_hatchery_pos);


    bool GetRandomUnit(const Unit*& unit_out, const ObservationInterface* observation, UnitTypeID unit_type);

    bool TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type); 
    bool TryBuildUnit(AbilityID ability_type_for_unit, UnitTypeID unit_type);

    const Unit* FindNearestLarva();
    const Unit* FindAvailableDrone();

    int CountUnitType(UNIT_TYPEID unit_type);
    Point2D FindEnemyBase();

    bool TryBuildZergling();
    void AttackWithZerglings(Point2D target);

    const Unit* FindNearestVespeneGeyser(const Point2D& start);


    bool AssignExtractorWorkers();
};


#endif