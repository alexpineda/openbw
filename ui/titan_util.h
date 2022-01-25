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
    size_t direction;
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
    size_t index;
    unsigned int unit_id_generation;
    int remainingTrainTime;
};

struct image_dump_t
{
    size_t index;
    int titanIndex;
    int typeId;
    int flags;
    int x; //pos
    int y; //pos
    int modifier;
    int modifierData1;
    int order;
    size_t frameIndex;
    size_t frameIndexOffset;
    size_t frameIndexBase;
};

struct sprite_dump_t
{
    size_t index;
    int titanIndex;
    int owner;
    int typeId;
    int selection_index;
    int visibility_flags;
    int elevation_level;
    int flags;
    int selection_timer;
    int width;
    int height;
    int x; //pos
    int y; //pos
    int mainImageIndex;
};