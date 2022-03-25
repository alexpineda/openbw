#include "../game_types.h"

struct unit_dump_t
{
    int id;
    int typeId;
    int owner;
    int x;
    int y;
    double hp;
    double energy;
    double shields;
    int spriteIndex;
    int statusFlags;
    int direction;
    int resourceAmount;
    int remainingBuildtime;
    int remainingTraintime;
    int kills;
    int order;
    int subunit;
    int orderState;
    int groundWeaponCooldown;
    int airWeaponCooldown;
    int spellCooldown;
    int index;
    unsigned int unit_id_generation;
    int remainingTrainTime;
};