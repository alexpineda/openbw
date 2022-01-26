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
    int index;
    int typeId;
    int flags;
    int x; //pos
    int y; //pos
    int modifier;
    int modifierData1;
    int order;
    int frameIndex;
    int frameIndexOffset;
    int frameIndexBase;
};

struct sprite_dump_t
{
    int index;
    int owner;
    int typeId;
    int elevation;
    int flags;
    int x; //pos
    int y; //pos
    int mainImageIndex;
    int imageCount;
    std::vector<image_dump_t> images;
};