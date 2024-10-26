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

    bool TryBuildHatchery();

    const Unit* FindNearestLarva();
    const Unit* FindAvailableDrone();

    int CountUnitType(UNIT_TYPEID unit_type);
};


#endif