#include "Engine/Objects/Character.h"

#include <algorithm>
#include <memory>

#include "Engine/Engine.h"
#include "Engine/AssetsManager.h"
#include "Engine/Spells/CastSpellInfo.h"
#include "Engine/Spells/Spells.h"
#include "Engine/Graphics/DecalBuilder.h"
#include "Engine/Graphics/Indoor.h"
#include "Engine/Graphics/Image.h"
#include "Engine/Graphics/IRender.h"
#include "Engine/Localization.h"
#include "Engine/LOD.h"
#include "Engine/MapInfo.h"
#include "Engine/Objects/Actor.h"
#include "Engine/Objects/ObjectList.h"
#include "Engine/Objects/SpriteObject.h"
#include "Engine/Objects/NPC.h"
#include "Engine/OurMath.h"
#include "Engine/Party.h"
#include "Engine/PriceCalculator.h"
#include "Engine/SpellFxRenderer.h"
#include "Engine/AttackList.h"
#include "Engine/Tables/ItemTable.h"
#include "Engine/Tables/CharacterFrameTable.h"
#include "Engine/Tables/StorylineTextTable.h"
#include "Engine/Tables/AwardTable.h"
#include "Engine/Tables/AutonoteTable.h"
#include "Engine/Tables/QuestTable.h"
#include "Engine/TurnEngine/TurnEngine.h"
#include "Engine/Conditions.h"

#include "Io/Mouse.h"

#include "Media/Audio/AudioPlayer.h"

#include "GUI/GUIWindow.h"
#include "GUI/GUIMessageQueue.h"
#include "GUI/UI/UIGame.h"
#include "GUI/UI/UIStatusBar.h"
#include "GUI/UI/UIMessageScroll.h"
#include "GUI/UI/Books/AutonotesBook.h"

#include "Library/Random/Random.h"

#include "Utility/Memory/MemSet.h"
#include "Utility/Math/FixPoint.h"
#include "Utility/IndexedArray.h"

static DecalBuilder *decal_builder = EngineIocContainer::ResolveDecalBuilder();
static SpellFxRenderer *spell_fx_renderer = EngineIocContainer::ResolveSpellFxRenderer();

// Race Stat Points Bonus/ Penalty
struct CharacterCreationAttributeProps {
    unsigned char uBaseValue;
    unsigned char uMaxValue;
    unsigned char uDroppedStep;
    unsigned char uBaseStep;
};

static constexpr CharacterCreationAttributeProps
    StatTable[4][7] =  // [human , elf, goblin, dwarf] [might, int, per , end,
                       // acc, speed, luck]
    {{
         {11, 25, 1, 1},
         {11, 25, 1, 1},
         {11, 25, 1, 1},
         {9, 25, 1, 1},
         {11, 25, 1, 1},
         {11, 25, 1, 1},
         {9, 25, 1, 1},
     },
     {
         {7, 15, 2, 1},
         {14, 30, 1, 2},
         {11, 25, 1, 1},
         {7, 15, 2, 1},
         {14, 30, 1, 2},
         {11, 25, 1, 1},
         {9, 20, 1, 1},
     },
     {
         {14, 30, 1, 2},
         {7, 15, 2, 1},
         {7, 15, 2, 1},
         {11, 25, 1, 1},
         {11, 25, 1, 1},
         {14, 30, 1, 2},
         {9, 20, 1, 1},
     },
     {{14, 30, 1, 2},
      {11, 25, 1, 1},
      {11, 25, 1, 1},
      {14, 30, 1, 2},
      {7, 15, 2, 1},
      {7, 15, 2, 1},
      {9, 20, 1, 1}}};

static constexpr IndexedArray<int, CHARACTER_SKILL_MASTERY_FIRST, CHARACTER_SKILL_MASTERY_LAST> StealingMasteryBonuses = {
    // {CHARACTER_SKILL_MASTERY_NONE, 0},
    {CHARACTER_SKILL_MASTERY_NOVICE, 100},
    {CHARACTER_SKILL_MASTERY_EXPERT, 200},
    {CHARACTER_SKILL_MASTERY_MASTER, 300},
    {CHARACTER_SKILL_MASTERY_GRANDMASTER, 500}
};  // dword_4EDEA0        //the zeroth element isn't accessed, it just
           // helps avoid -1 indexing, originally 4 element array off by one
static constexpr std::array<int, 5> StealingRandomBonuses = { -200, -100, 0, 100, 200 };  // dword_4EDEB4

static constexpr IndexedArray<int, CHARACTER_SKILL_MASTERY_FIRST, CHARACTER_SKILL_MASTERY_LAST> StealingEnchantmentBonusForSkill = {
    // {CHARACTER_SKILL_MASTERY_NONE, 0},
    {CHARACTER_SKILL_MASTERY_NOVICE, 2},
    {CHARACTER_SKILL_MASTERY_EXPERT, 4},
    {CHARACTER_SKILL_MASTERY_MASTER, 6},
    {CHARACTER_SKILL_MASTERY_GRANDMASTER, 10}
};  // dword_4EDEC4      //the zeroth element isn't accessed, it just
          // helps avoid -1 indexing, originally 4 element array off by one

static constexpr IndexedArray<ITEM_SLOT, EQUIP_FIRST, EQUIP_LAST> pEquipTypeToBodyAnchor = {  // 4E8398
    {EQUIP_SINGLE_HANDED,  ITEM_SLOT_MAIN_HAND},
    {EQUIP_TWO_HANDED,     ITEM_SLOT_MAIN_HAND},
    {EQUIP_BOW,            ITEM_SLOT_BOW},
    {EQUIP_ARMOUR,         ITEM_SLOT_ARMOUR},
    {EQUIP_SHIELD,         ITEM_SLOT_OFF_HAND},
    {EQUIP_HELMET,         ITEM_SLOT_HELMET},
    {EQUIP_BELT,           ITEM_SLOT_BELT},
    {EQUIP_CLOAK,          ITEM_SLOT_CLOAK},
    {EQUIP_GAUNTLETS,      ITEM_SLOT_GAUTNLETS},
    {EQUIP_BOOTS,          ITEM_SLOT_BOOTS},
    {EQUIP_RING,           ITEM_SLOT_RING1},
    {EQUIP_AMULET,         ITEM_SLOT_AMULET},
    {EQUIP_WAND,           ITEM_SLOT_MAIN_HAND},
    {EQUIP_REAGENT,        ITEM_SLOT_INVALID},
    {EQUIP_POTION,         ITEM_SLOT_INVALID},
    {EQUIP_SPELL_SCROLL,   ITEM_SLOT_INVALID},
    {EQUIP_BOOK,           ITEM_SLOT_INVALID},
    {EQUIP_MESSAGE_SCROLL, ITEM_SLOT_INVALID},
    {EQUIP_GOLD,           ITEM_SLOT_INVALID},
    {EQUIP_GEM,            ITEM_SLOT_INVALID},
    {EQUIP_NONE,           ITEM_SLOT_INVALID}
};

static constexpr unsigned char pBaseHealthByClass[12] = {40, 35, 35, 30, 30, 30,
                                        25, 20, 20, 0,  0,  0};
static constexpr unsigned char pBaseManaByClass[12] = {0, 0, 0, 5, 5, 0, 10, 10, 15, 0, 0, 0};
static constexpr unsigned char pBaseHealthPerLevelByClass[36] = {
    5, 7, 9, 9, 4, 6, 8, 8, 5, 6, 8, 8, 4, 5, 6, 6, 3, 4,
    6, 6, 4, 5, 6, 6, 2, 3, 4, 4, 2, 3, 4, 4, 2, 3, 3, 3};
static constexpr unsigned char pBaseManaPerLevelByClass[36] = {
    0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 2, 3, 3, 1, 2,
    3, 3, 0, 2, 3, 3, 3, 4, 5, 5, 3, 4, 5, 5, 3, 4, 6, 6};

static constexpr unsigned char pConditionAttributeModifier[7][19] = {
    {100, 100, 100, 120, 50, 200, 75, 60, 50, 30, 25, 10, 100, 100, 100, 100,
     100, 100, 100},  // Might
    {100, 100, 100, 50, 25, 10, 100, 100, 75, 60, 50, 30, 100, 100, 100, 100,
     100, 1, 100},  // Intelligence
    {100, 100, 100, 50, 25, 10, 100, 100, 75, 60, 50, 30, 100, 100, 100, 100,
     100, 1, 100},  // Personality
    {100, 100, 100, 100, 50, 150, 75, 60, 50, 30, 25, 10, 100, 100, 100, 100,
     100, 100, 100},  // Endurance
    {100, 100, 100, 50, 10, 100, 75, 60, 50, 30, 25, 10, 100, 100, 100, 100,
     100, 50, 100},  // Accuracy
    {100, 100, 100, 120, 20, 120, 75, 60, 50, 30, 25, 10, 100, 100, 100, 100,
     100, 50, 100},  // Speed
    {100, 100, 100, 100, 200, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
     100, 100, 100, 100}};  // Luck

static constexpr unsigned char pAgingAttributeModifier[7][4] = {
    {100, 75, 40, 10},      // Might
    {100, 150, 100, 10},    // Intelligence
    {100, 150, 100, 10},    // Personality
    {100, 75, 40, 10},      // Endurance
    {100, 100, 40, 10},     // Accuracy
    {100, 100, 40, 10},     // Speed
    {100, 100, 100, 100}};  // Luck

static constexpr unsigned int pAgeingTable[4] = {50, 100, 150, 0xFFFF};

static constexpr short param_to_bonus_table[29] = {
    500, 400, 350, 300, 275, 250, 225, 200, 175, 150, 125, 100, 75, 50, 40,
    35,  30,  25,  21,  19,  17,  15,  13,  11,  9,   7,   5,   3,  0};
static constexpr signed int parameter_to_bonus_value[29] = {
    30, 25, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8,
    7,  6,  5,  4,  3,  2,  1,  0,  -1, -2, -3, -4, -5, -6};

//----- (00490913) --------------------------------------------------------
int CharacterCreation_GetUnspentAttributePointCount() {
    int CurrentStatValue = 50;
    int RemainingStatPoints = 50;
    int raceId;
    int StatBaseValue;
    int PenaltyMult;
    int BonusMult;

    for (Character &character : pParty->pCharacters) {
        raceId = character.GetRace();

        for (int statNum = 0; statNum <= 6; statNum++) {
            switch (statNum) {
                case 0:
                    CurrentStatValue = character.uMight;
                    break;
                case 1:
                    CurrentStatValue = character.uIntelligence;
                    break;
                case 2:
                    CurrentStatValue = character.uPersonality;
                    break;
                case 3:
                    CurrentStatValue = character.uEndurance;
                    break;
                case 4:
                    CurrentStatValue = character.uAccuracy;
                    break;
                case 5:
                    CurrentStatValue = character.uSpeed;
                    break;
                case 6:
                    CurrentStatValue = character.uLuck;
                    break;
            }

            StatBaseValue = StatTable[raceId][statNum].uBaseValue;

            if (CurrentStatValue >=
                StatBaseValue) {  // bonus or penalty increase
                PenaltyMult = StatTable[raceId][statNum].uDroppedStep;
                BonusMult = StatTable[raceId][statNum].uBaseStep;
            } else {  // less than base
                PenaltyMult = StatTable[raceId][statNum].uBaseStep;
                BonusMult = StatTable[raceId][statNum].uDroppedStep;
            }

            RemainingStatPoints +=
                PenaltyMult * (StatBaseValue - CurrentStatValue) / BonusMult;
        }
    }

    return RemainingStatPoints;
}

//----- (00427730) --------------------------------------------------------
bool Character::CanCastSpell(unsigned int uRequiredMana) {
    if (engine->config->debug.AllMagic.value()) {
        return true;
    }
    if (mana >= uRequiredMana) {  // enough mana
        return true;
    }

    // not enough mana
    return false;
}

void Character::SpendMana(unsigned int uRequiredMana) {
    if (engine->config->debug.AllMagic.value()) {
        return;
    }
    assert(mana >= uRequiredMana);
    mana -= uRequiredMana; // remove mana required for spell
}

//----- (004BE2DD) --------------------------------------------------------
void Character::SalesProcess(unsigned int inventory_idnx, int item_index, HOUSE_ID houseId) {
    float shop_mult = buildingTable[houseId].fPriceMultiplier;
    int sell_price = PriceCalculator::itemSellingPriceForPlayer(this, pOwnItems[item_index], shop_mult);

    // remove item and add gold
    RemoveItemAtInventoryIndex(inventory_idnx);
    pParty->AddGold(sell_price);
}

//----- (0043EEF3) --------------------------------------------------------
bool Character::NothingOrJustBlastersEquipped() const {
    signed int item_idx;
    ITEM_TYPE item_id;

    // scan through all equipped items
    for (ITEM_SLOT i : allItemSlots()) {
        item_idx = pEquipment.pIndices[i];

        if (item_idx) {
            item_id = pOwnItems[item_idx - 1].uItemID;

            if (item_id != ITEM_BLASTER &&
                item_id != ITEM_BLASTER_RIFLE)  // soemthing other than blaster&
                                              // blaster rifle
                return false;
        }
    }

    return true;  // nothing or just blaster equipped
}

//----- (004B8040) --------------------------------------------------------
int Character::GetConditionDaysPassed(Condition condition) const {
    // PS - CHECK ?? is this the intedned behavior - RETURN
    // NUMBER OF DAYS CONDITION HAS BEEN ACTIVE FOR

    if (!this->conditions.Has(condition))
        return 0;

    GameTime playtime = pParty->GetPlayingTime();
    GameTime condtime = this->conditions.Get(condition);
    GameTime diff = playtime - condtime;

    return diff.GetDays() + 1;
}

ItemGen *Character::GetItemAtInventoryIndex(int inout_item_cell) {
    int inventory_index = this->GetItemListAtInventoryIndex(inout_item_cell);

    if (!inventory_index) {
        return nullptr;
    }

    return &this->pInventoryItemList[inventory_index - 1];
}

//----- (00421E75) --------------------------------------------------------
unsigned int Character::GetItemListAtInventoryIndex(int inout_item_cell) {
    int cell_idx = inout_item_cell;
    if (cell_idx > 125 || cell_idx < 0) return 0;

    int inventory_index = this->pInventoryMatrix[cell_idx];
    if (inventory_index < 0) {  // not pointed to main item cell so redirect
        inventory_index = this->pInventoryMatrix[-1 - inventory_index];
    }

    return inventory_index;  // returns item list position + 1
}

unsigned int Character::GetItemMainInventoryIndex(int inout_item_cell) {
    int cell_idx = inout_item_cell;
    if (cell_idx > 125 || cell_idx < 0) return 0;

    int inventory_index = this->pInventoryMatrix[cell_idx];
    if (inventory_index < 0) {  // not pointed to main item cell so redirect
        cell_idx = (-(inventory_index + 1));
    }

    return cell_idx;
}

//----- (004160CA) --------------------------------------------------------
void Character::ItemsPotionDmgBreak(int enchant_count) {
    int avalible_items = 0;

    int16_t item_index_tabl[TOTAL_ITEM_SLOT_COUNT];  // table holding items
    memset(item_index_tabl, 0, sizeof(item_index_tabl));  // set to zero

    for (int i = 0; i < TOTAL_ITEM_SLOT_COUNT; ++i)  // scan through and log in table
        if (isRegular(pOwnItems[i].uItemID))
            item_index_tabl[avalible_items++] = i;

    if (avalible_items) {  // is there anything to break
        if (enchant_count) {
            for (int i = 0; i < enchant_count; ++i) {
                int indexbreak =
                    item_index_tabl[grng->random(avalible_items)];  // random item

                if (!(pInventoryItemList[indexbreak].uAttributes &
                      ITEM_HARDENED))  // if its not hardened
                    pInventoryItemList[indexbreak].uAttributes |=
                        ITEM_BROKEN;  // break it
            }
        } else {
            for (int i = 0; i < avalible_items; ++i) {  // break everything
                pInventoryItemList[item_index_tabl[i]].uAttributes |=
                    ITEM_BROKEN;
            }
        }
    }
}

//----- (00492C0B) --------------------------------------------------------
bool Character::CanAct() const {
    if (this->IsAsleep() || this->IsParalyzed() || this->IsUnconcious() ||
        this->IsDead() || this->IsPertified() || this->IsEradicated())

        return false;
    else
        return true;
}

//----- (00492C40) --------------------------------------------------------
bool Character::CanSteal() const {
    return getActualSkillValue(CHARACTER_SKILL_STEALING).level() != 0;
}

//----- (00492C4E) --------------------------------------------------------
bool Character::CanEquip_RaceAndAlignmentCheck(ITEM_TYPE uItemID) const {
    switch (uItemID) {
        case ITEM_RELIC_ETHRICS_STAFF:
        case ITEM_RELIC_OLD_NICK:
        case ITEM_RELIC_TWILIGHT:
            return pParty->isPartyEvil();
            break;

        case ITEM_RELIC_TALEDONS_HELM:
        case ITEM_RELIC_JUSTICE:
            return pParty->isPartyGood();
            break;

        case ITEM_ARTIFACT_ELFBANE:
            return IsRaceGoblin();
            break;

        case ITEM_ARTIFACT_MINDS_EYE:
            return IsRaceHuman();
            break;

        case ITEM_ARTIFACT_ELVEN_CHAINMAIL:
            return IsRaceElf();
            break;

        case ITEM_ARTIFACT_FORGE_GAUNTLETS:
            return IsRaceDwarf();
            break;

        case ITEM_ARTIFACT_HEROS_BELT:
            return IsMale();
            break;

        case ITEM_ARTIFACT_LADYS_ESCORT:
            return IsFemale();
            break;

        case ITEM_QUEST_WETSUIT:
            return NothingOrJustBlastersEquipped();
            break;

        default:
            return 1;
            break;
    }
}

//----- (00492D65) --------------------------------------------------------
void Character::SetCondition(Condition condition, int blockable) {
    if (conditions.Has(condition))  // cant get the same condition twice
        return;

    if (blockable && blockCondition(this, condition))
        return;

    switch (condition) {  // conditions noises
        case CONDITION_CURSED:
            playReaction(SPEECH_CURSED);
            break;
        case CONDITION_WEAK:
            playReaction(SPEECH_WEAK);
            break;
        case CONDITION_SLEEP:
            break;  // nosound
        case CONDITION_FEAR:
            playReaction(SPEECH_FEAR);
            break;
        case CONDITION_DRUNK:
            playReaction(SPEECH_DRUNK);
            break;
        case CONDITION_INSANE:
            playReaction(SPEECH_INSANE);
            break;

        case CONDITION_POISON_WEAK:
        case CONDITION_POISON_MEDIUM:
        case CONDITION_POISON_SEVERE:
            playReaction(SPEECH_POISONED);
            break;

        case CONDITION_DISEASE_WEAK:
        case CONDITION_DISEASE_MEDIUM:
        case CONDITION_DISEASE_SEVERE:
            playReaction(SPEECH_DISEASED);
            break;

        case CONDITION_PARALYZED:
            break;  // nosound

        case CONDITION_UNCONSCIOUS:
            playReaction(SPEECH_UNCONSCIOUS);
            if (health > 0) {
                health = 0;
            }
            break;

        case CONDITION_DEAD:
            playReaction(SPEECH_DEAD);
            if (health > 0) {
                health = 0;
            }
            if (mana > 0) {
                mana = 0;
            }
            break;

        case CONDITION_PETRIFIED:
            playReaction(SPEECH_PETRIFIED);
            break;

        case CONDITION_ERADICATED:
            playReaction(SPEECH_ERADICATED);
            if (health > 0) {
                health = 0;
            }
            if (mana > 0) {
                mana = 0;
            }
            break;

        case CONDITION_ZOMBIE:
            if (classType == CHARACTER_CLASS_LICH || IsEradicated() || IsZombie() || !IsDead()) { // cant zombified
                return;
            }

            conditions.ResetAll();
            health = GetMaxHealth();
            mana = 0;
            uPrevFace = uCurrentFace;
            uPrevVoiceID = uVoiceID;

            if (IsMale()) {
                uCurrentFace = 23;
                uVoiceID = 23;
            } else {
                uCurrentFace = 24;
                uVoiceID = 24;
            }

            playReaction(SPEECH_CHEATED_DEATH);
            break;

        default:
            break;
    }

    int playersBefore = 0;
    for (Character &character : pParty->pCharacters) {  // count active players before activating condition
        playersBefore += character.CanAct() ? 1 : 0;
    }

    conditions.Set(condition, pParty->GetPlayingTime());  // set condition

    int playersAfter = 0;
    Character *remainingPlayer = nullptr;
    for (Character &character : pParty->pCharacters) {
        if (character.CanAct()) {
            remainingPlayer = &character;
            playersAfter++;
        }
    }

    if ((playersBefore == 2) && (playersAfter == 1)) { // if was 2 and now down to 1 - "its just you and me now"
        remainingPlayer->playReaction(SPEECH_LAST_MAN_STANDING);
    }

    return;
}

bool Character::canFitItem(unsigned int uSlot, ITEM_TYPE uItemID) const {
    auto img = assets->getImage_ColorKey(pItemTable->pItems[uItemID].iconName);
    unsigned int slotWidth = GetSizeInInventorySlots(img->width());
    unsigned int slotHeight = GetSizeInInventorySlots(img->height());

    Assert(slotHeight > 0 && slotWidth > 0,
           "Items should have nonzero dimensions");
    if ((slotWidth + uSlot % INVENTORY_SLOTS_WIDTH) <= INVENTORY_SLOTS_WIDTH &&
        (slotHeight + uSlot / INVENTORY_SLOTS_WIDTH) <= INVENTORY_SLOTS_HEIGHT) {
        for (unsigned int x = 0; x < slotWidth; x++) {
            for (unsigned int y = 0; y < slotHeight; y++) {
                if (pInventoryMatrix[y * INVENTORY_SLOTS_WIDTH + x + uSlot] != 0) {
                    return false;
                }
            }
        }
        return true;
    }
    return false;
}

int Character::findFreeInventoryListSlot() const {
    for (int i = 0; i < INVENTORY_SLOT_COUNT; i++) {
        if (pInventoryItemList[i].uItemID == ITEM_NULL) {
            return i;  // space at i
        }
    }

    return -1;  // no room
}

//----- (00492600) --------------------------------------------------------
int Character::CreateItemInInventory(unsigned int uSlot, ITEM_TYPE uItemID) {
    signed int freeSlot = findFreeInventoryListSlot();

    if (freeSlot == -1) {  // no room
        if (pParty->hasActiveCharacter()) {
            pParty->activeCharacter().playReaction(SPEECH_NO_ROOM);
        }

        return 0;
    } else {  // place items
        PutItemArInventoryIndex(uItemID, freeSlot, uSlot);
        this->pInventoryItemList[freeSlot].uItemID = uItemID;
    }

    return freeSlot + 1;  // return slot no + 1
}

//----- (00492700) --------------------------------------------------------
int Character::HasSkill(CharacterSkillType skill) const {
    if (this->pActiveSkills[skill]) {
        return 1;
    } else {
        // TODO(captainurist): this doesn't belong to a getter!!!
        GameUI_SetStatusBar(LSTR_FMT_S_DOES_NOT_HAVE_SKILL, this->name);
        return 0;
    }
}

//----- (00492745) --------------------------------------------------------
void Character::WearItem(ITEM_TYPE uItemID) {
    int item_indx = findFreeInventoryListSlot();

    if (item_indx != -1) {
        pInventoryItemList[item_indx].uItemID = uItemID;
        ITEM_SLOT item_body_anch = pEquipTypeToBodyAnchor[pItemTable->pItems[uItemID].uEquipType];
        pEquipment.pIndices[item_body_anch] = item_indx + 1;
        pInventoryItemList[item_indx].uBodyAnchor = item_body_anch;
    }
}

//----- (004927A8) --------------------------------------------------------
int Character::AddItem(int index, ITEM_TYPE uItemID) {
    if (uItemID == ITEM_NULL) {
        return 0;
    }

    if (index == -1) {  // no location specified - search for space
        for (int xcoord = 0; xcoord < INVENTORY_SLOTS_WIDTH; xcoord++) {
            for (int ycoord = 0; ycoord < INVENTORY_SLOTS_HEIGHT; ycoord++) {
                if (canFitItem(ycoord * INVENTORY_SLOTS_WIDTH + xcoord, uItemID)) {  // found space
                    return CreateItemInInventory(ycoord * INVENTORY_SLOTS_WIDTH + xcoord, uItemID);
                }
            }
        }

        return 0;  // no space cant add item
    }

    if (!canFitItem(index, uItemID)) {
        pAudioPlayer->playUISound(SOUND_error);
        return 0;  // cant fit item
    }

    return CreateItemInInventory(index, uItemID);  // return location
}

//----- (00492826) --------------------------------------------------------
int Character::AddItem2(int index, ItemGen *Src) {  // are both required - check
    pItemTable->SetSpecialBonus(Src);

    if (index == -1) {  // no loaction specified
        for (int xcoord = 0; xcoord < INVENTORY_SLOTS_WIDTH; xcoord++) {
            for (int ycoord = 0; ycoord < INVENTORY_SLOTS_HEIGHT; ycoord++) {
                if (canFitItem(ycoord * INVENTORY_SLOTS_WIDTH + xcoord,
                               Src->uItemID)) {  // found space
                    return CreateItemInInventory2(
                        ycoord * INVENTORY_SLOTS_WIDTH + xcoord, Src);
                }
            }
        }

        return 0;
    }

    if (!canFitItem(index, Src->uItemID)) return 0;

    return CreateItemInInventory2(index, Src);
}

//----- (0049289C) --------------------------------------------------------
int Character::CreateItemInInventory2(unsigned int index,
                                   ItemGen *Src) {  // are both required - check
    signed int freeSlot = findFreeInventoryListSlot();
    int result;

    if (freeSlot == -1) {  // no room
        result = 0;
    } else {
        PutItemArInventoryIndex(Src->uItemID, freeSlot, index);
        memcpy(&pInventoryItemList[freeSlot], Src, sizeof(ItemGen));
        result = freeSlot + 1;
    }

    return result;
}

//----- (0049298B) --------------------------------------------------------
void Character::PutItemArInventoryIndex(
    ITEM_TYPE uItemID, int itemListPos,
    int index) {  // originally accepted ItemGen *but needed only its uItemID

    auto img = assets->getImage_ColorKey(pItemTable->pItems[uItemID].iconName);
    unsigned int slot_width = GetSizeInInventorySlots(img->width());
    unsigned int slot_height = GetSizeInInventorySlots(img->height());

    if (slot_width > 0) {
        int *pInvPos = &pInventoryMatrix[index];
        for (unsigned int i = 0; i < slot_height; i++) {
            memset32(pInvPos, -1 - index,
                     slot_width);  // TODO(_): try to come up with a better
                                   // solution. negative values are used when
                                   // drawing the inventory - nothing is drawn
            pInvPos += INVENTORY_SLOTS_WIDTH;
        }
    }

    pInventoryMatrix[index] = itemListPos + 1;
}

//----- (00492A36) --------------------------------------------------------
void Character::RemoveItemAtInventoryIndex(unsigned int index) {
    ItemGen *item_in_slot = this->GetItemAtInventoryIndex(index);

    auto img = assets->getImage_ColorKey(item_in_slot->GetIconName());
    unsigned int slot_width = GetSizeInInventorySlots(img->width());
    unsigned int slot_height = GetSizeInInventorySlots(img->height());

    item_in_slot->Reset();  // must get img details before reset

    int inventory_index = this->pInventoryMatrix[index];
    if (inventory_index < 0) {  // not pointed to main item cell so redirect
        index = (-1 - inventory_index);
    }

    if (slot_width > 0) {
        int *pInvPos = &pInventoryMatrix[index];
        for (unsigned int i = 0; i < slot_height; i++) {
            memset32(pInvPos, 0, slot_width);
            pInvPos += INVENTORY_SLOTS_WIDTH;
        }
    }
}

//----- (0049107D) --------------------------------------------------------
int Character::GetBodybuilding() const {
    int multiplier =
        GetMultiplierForSkillLevel(CHARACTER_SKILL_BODYBUILDING, 1, 2, 3, 5);

    return multiplier * getActualSkillValue(CHARACTER_SKILL_BODYBUILDING).level();
}

//----- (004910A8) --------------------------------------------------------
int Character::GetMeditation() const {
    int multiplier =
        GetMultiplierForSkillLevel(CHARACTER_SKILL_MEDITATION, 1, 2, 3, 5);

    return multiplier * getActualSkillValue(CHARACTER_SKILL_MEDITATION).level();
}

//----- (004910D3) --------------------------------------------------------
bool Character::CanIdentify(ItemGen *pItem) const {
    CombinedSkillValue val = getActualSkillValue(CHARACTER_SKILL_ITEM_ID);
    int multiplier =
        GetMultiplierForSkillLevel(CHARACTER_SKILL_ITEM_ID, 1, 2, 3, 5);

    if (CheckHiredNPCSpeciality(Scholar) || val.mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER)  // always identify
        return true;

    // check item level against skill
    bool result = (multiplier * val.level()) >=
                  pItemTable->pItems[pItem->uItemID].uItemID_Rep_St;

    return result;
}

//----- (00491151) --------------------------------------------------------
bool Character::CanRepair(ItemGen *pItem) const {
    CombinedSkillValue val = getActualSkillValue(CHARACTER_SKILL_REPAIR);
    int multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_REPAIR, 1, 2, 3, 5);

    // TODO(Nik-RE-dev): is check for boots correct?
    if (CheckHiredNPCSpeciality(Smith) && pItem->isWeapon() ||
        CheckHiredNPCSpeciality(Armorer) && pItem->isArmor() ||
        CheckHiredNPCSpeciality(Alchemist) && pItem->GetItemEquipType() >= EQUIP_BOOTS)
        return true;  // check against hired help

    if (val.mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER)  // gm repair
        return true;

    // check item level against skill
    bool result = (multiplier * val.level()) >=
                  pItemTable->pItems[pItem->uItemID].uItemID_Rep_St;

    return result;
}

//----- (0049125A) --------------------------------------------------------
int Character::GetPerception() const {
    CombinedSkillValue val = getActualSkillValue(CHARACTER_SKILL_PERCEPTION);
    int multiplier =
        GetMultiplierForSkillLevel(CHARACTER_SKILL_PERCEPTION, 1, 2, 3, 5);

    if (val.mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER)  // gm percept
        return 10000;

    return multiplier * val.level();
}

//----- (004912B0) --------------------------------------------------------
int Character::GetDisarmTrap() const {
    CombinedSkillValue val = getActualSkillValue(CHARACTER_SKILL_TRAP_DISARM);
    int multiplier =
        GetMultiplierForSkillLevel(CHARACTER_SKILL_TRAP_DISARM, 1, 2, 3, 5);

    if (val.mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER)  // gm disarm
        return 10000;

    if (HasEnchantedItemEquipped(ITEM_ENCHANTMENT_OF_THIEVERY))  // item has increased disarm
        multiplier++;

    return multiplier * val.level();
}

char Character::getLearningPercent() const {
    int skill = getActualSkillValue(CHARACTER_SKILL_LEARNING).level();

    if (skill) {
        int multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_LEARNING, 1, 2, 3, 5);

        return multiplier * skill + 9;
    } else {
        return 0;
    }
}

//----- (0048C855) --------------------------------------------------------
int Character::GetBaseMight() const {
    return this->uMight + GetItemsBonus(CHARACTER_ATTRIBUTE_MIGHT);
}

//----- (0048C86C) --------------------------------------------------------
int Character::GetBaseIntelligence() const {
    return this->uIntelligence +
           GetItemsBonus(CHARACTER_ATTRIBUTE_INTELLIGENCE);
}

//----- (0048C883) --------------------------------------------------------
int Character::GetBasePersonality() const {
    return this->uPersonality + GetItemsBonus(CHARACTER_ATTRIBUTE_PERSONALITY);
}

//----- (0048C89A) --------------------------------------------------------
int Character::GetBaseEndurance() const {
    return this->uEndurance + GetItemsBonus(CHARACTER_ATTRIBUTE_ENDURANCE);
}

//----- (0048C8B1) --------------------------------------------------------
int Character::GetBaseAccuracy() const {
    return this->uAccuracy + GetItemsBonus(CHARACTER_ATTRIBUTE_ACCURACY);
}

//----- (0048C8C8) --------------------------------------------------------
int Character::GetBaseSpeed() const {
    return this->uSpeed + GetItemsBonus(CHARACTER_ATTRIBUTE_SPEED);
}

//----- (0048C8DF) --------------------------------------------------------
int Character::GetBaseLuck() const {
    return this->uLuck + GetItemsBonus(CHARACTER_ATTRIBUTE_LUCK);
}

//----- (0048C8F6) --------------------------------------------------------
int Character::GetBaseLevel() const {
    return this->uLevel + GetItemsBonus(CHARACTER_ATTRIBUTE_LEVEL);
}

//----- (0048C90D) --------------------------------------------------------
int Character::GetActualLevel() const {
    return uLevel + sLevelModifier +
           GetMagicalBonus(CHARACTER_ATTRIBUTE_LEVEL) +
           GetItemsBonus(CHARACTER_ATTRIBUTE_LEVEL);
}

//----- (0048C93C) --------------------------------------------------------
int Character::GetActualMight() const {
    return GetActualAttribute(CHARACTER_ATTRIBUTE_MIGHT, &Character::uMight,
                              &Character::uMightBonus);
}

//----- (0048C9C2) --------------------------------------------------------
int Character::GetActualIntelligence() const {
    return GetActualAttribute(CHARACTER_ATTRIBUTE_INTELLIGENCE,
                              &Character::uIntelligence,
                              &Character::uIntelligenceBonus);
}

//----- (0048CA3F) --------------------------------------------------------
int Character::GetActualPersonality() const {
    return GetActualAttribute(CHARACTER_ATTRIBUTE_PERSONALITY,
                              &Character::uPersonality, &Character::uPersonalityBonus);
}

//----- (0048CABC) --------------------------------------------------------
int Character::GetActualEndurance() const {
    return GetActualAttribute(CHARACTER_ATTRIBUTE_ENDURANCE,
                              &Character::uEndurance, &Character::uEnduranceBonus);
}

//----- (0048CB39) --------------------------------------------------------
int Character::GetActualAccuracy() const {
    return GetActualAttribute(CHARACTER_ATTRIBUTE_ACCURACY, &Character::uAccuracy,
                              &Character::uAccuracyBonus);
}

//----- (0048CBB6) --------------------------------------------------------
int Character::GetActualSpeed() const {
    return GetActualAttribute(CHARACTER_ATTRIBUTE_SPEED, &Character::uSpeed,
                              &Character::uSpeedBonus);
}

//----- (0048CC33) --------------------------------------------------------
int Character::GetActualLuck() const {
    signed int npc_luck_bonus = 0;

    if (CheckHiredNPCSpeciality(Fool)) npc_luck_bonus = 5;

    if (CheckHiredNPCSpeciality(ChimneySweep)) npc_luck_bonus += 20;

    if (CheckHiredNPCSpeciality(Psychic)) npc_luck_bonus += 10;

    return GetActualAttribute(CHARACTER_ATTRIBUTE_LUCK, &Character::uLuck,
                              &Character::uLuckBonus) +
           npc_luck_bonus;
}

//----- (new function) --------------------------------------------------------
int Character::GetActualAttribute(CharacterAttributeType attrId,
                               unsigned short Character::*attrValue,
                               unsigned short Character::*attrBonus) const {
    uint uActualAge = this->sAgeModifier + GetBaseAge();
    uint uAgeingMultiplier = 100;

    for (uint i = 0; i < 4; ++i) {
        if (uActualAge >=
            pAgeingTable[i])  // is the character old enough to need attrib adjust
            uAgeingMultiplier = pAgingAttributeModifier[attrId][i];
        else
            break;
    }

    uchar uConditionMult = pConditionAttributeModifier
        [attrId][std::to_underlying(GetMajorConditionIdx())];  // weak from disease or poison ect
    int magicBonus = GetMagicalBonus(attrId);
    int itemBonus = GetItemsBonus(attrId);

    return uConditionMult * uAgeingMultiplier * this->*attrValue / 100 / 100 +
           magicBonus + itemBonus + this->*attrBonus;
}

//----- (0048CCF5) --------------------------------------------------------
int Character::GetActualAttack(bool onlyMainHandDmg) const {
    int parbonus = GetParameterBonus(
        GetActualAccuracy());  // bonus points for steps of accuracy level
    int atkskillbonus = GetSkillBonus(
        CHARACTER_ATTRIBUTE_ATTACK);  // bonus for skill with weapon
    int weapbonus = GetItemsBonus(CHARACTER_ATTRIBUTE_ATTACK,
                                  onlyMainHandDmg);  // how good is weapon

    return parbonus + atkskillbonus + weapbonus +
           GetMagicalBonus(CHARACTER_ATTRIBUTE_ATTACK) +
           this->_some_attack_bonus;
}

//----- (0048CD45) --------------------------------------------------------
int Character::GetMeleeDamageMinimal() const {
    int parbonus = GetParameterBonus(GetActualMight());
    int weapbonus = GetItemsBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_MIN) + parbonus;
    int atkskillbonus =
        GetSkillBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS) + weapbonus;

    int result = _melee_dmg_bonus +
                 GetMagicalBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS) +
                 atkskillbonus;

    if (result < 1) result = 1;

    return result;
}

//----- (0048CD90) --------------------------------------------------------
int Character::GetMeleeDamageMaximal() const {
    int parbonus = GetParameterBonus(GetActualMight());
    int weapbonus = GetItemsBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_MAX) + parbonus;
    int atkskillbonus =
        GetSkillBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS) + weapbonus;

    int result = this->_melee_dmg_bonus +
                 GetMagicalBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS) +
                 atkskillbonus;

    if (result < 1) result = 1;

    return result;
}

//----- (0048CDDB) --------------------------------------------------------
int Character::CalculateMeleeDamageTo(bool ignoreSkillBonus, bool ignoreOffhand,
                                   unsigned int uTargetActorID) {
    int mainWpnDmg = 0;
    int offHndWpnDmg = 0;

    if (IsUnarmed()) {  // no weapons
        mainWpnDmg = grng->random(3) + 1;
    } else {
        if (HasItemEquipped(ITEM_SLOT_MAIN_HAND)) {
            ItemGen *mainHandItemGen = this->GetMainHandItem();
            ITEM_TYPE itemId = mainHandItemGen->uItemID;
            bool addOneDice = false;
            if (pItemTable->pItems[itemId].uSkillType == CHARACTER_SKILL_SPEAR &&
                !this->pEquipment
                     .uOffHand)  // using spear in two hands adds a dice roll
                addOneDice = true;

            mainWpnDmg = CalculateMeleeDmgToEnemyWithWeapon(
                mainHandItemGen, uTargetActorID, addOneDice);
        }

        if (!ignoreOffhand) {
            if (this->HasItemEquipped(ITEM_SLOT_OFF_HAND)) {  // has second hand got a weapon
                                                              // that not a shield
                ItemGen *offHandItemGen =
                    (ItemGen*)&this
                        ->pInventoryItemList[this->pEquipment.uOffHand - 1];

                if (!offHandItemGen->isShield()) {
                    offHndWpnDmg = CalculateMeleeDmgToEnemyWithWeapon(
                        offHandItemGen, uTargetActorID, false);
                }
            }
        }
    }

    int dmgSum = mainWpnDmg + offHndWpnDmg;

    if (!ignoreSkillBonus) {
        int mightBonus = GetParameterBonus(GetActualMight());
        int mightAndSkillbonus =
            GetSkillBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS) + mightBonus;
        dmgSum += this->_melee_dmg_bonus +
                  GetMagicalBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS) +
                  mightAndSkillbonus;
    }

    if (dmgSum < 1) dmgSum = 1;

    return dmgSum;
}

int Character::CalculateMeleeDmgToEnemyWithWeapon(ItemGen *weapon,
                                               unsigned int uTargetActorID,
                                               bool addOneDice) {
    ITEM_TYPE itemId = weapon->uItemID;
    int diceCount = pItemTable->pItems[itemId].uDamageDice;

    if (addOneDice) diceCount++;

    int diceSides = pItemTable->pItems[itemId].uDamageRoll;
    int diceResult = 0;

    for (int i = 0; i < diceCount; i++) {  // roll dice
        diceResult += grng->random(diceSides) + 1;
    }

    int totalDmg =
            pItemTable->pItems[itemId].uDamageMod + diceResult;  // add modifer

    if (uTargetActorID > 0) {  // if an actor has been provided
        ITEM_ENCHANTMENT enchType =
            weapon->special_enchantment;  // check against enchantments

        if (MonsterStats::BelongsToSupertype(uTargetActorID,
                                             MONSTER_SUPERTYPE_UNDEAD) &&
            (enchType == ITEM_ENCHANTMENT_UNDEAD_SLAYING || itemId == ITEM_ARTIFACT_GHOULSBANE ||
             itemId == ITEM_ARTIFACT_GIBBET || itemId == ITEM_RELIC_JUSTICE)) {
            totalDmg *= 2;  // double damage vs undead
        } else if (MonsterStats::BelongsToSupertype(uTargetActorID, MONSTER_SUPERTYPE_KREEGAN) &&
                   (enchType == ITEM_ENCHANTMENT_DEMON_SLAYING || itemId == ITEM_ARTIFACT_GIBBET)) {
            totalDmg *= 2;  // double damage vs devils
        } else if (MonsterStats::BelongsToSupertype(uTargetActorID, MONSTER_SUPERTYPE_DRAGON) &&
                   (enchType == ITEM_ENCHANTMENT_DRAGON_SLAYING || itemId == ITEM_ARTIFACT_GIBBET)) {
            totalDmg *= 2;  // double damage vs dragons
        } else if (MonsterStats::BelongsToSupertype(uTargetActorID, MONSTER_SUPERTYPE_ELF) &&
                   (enchType == ITEM_ENCHANTMENT_ELF_SLAYING || itemId == ITEM_RELIC_OLD_NICK)) {
            totalDmg *= 2;  // double damage vs elf
        } else if (MonsterStats::BelongsToSupertype(uTargetActorID, MONSTER_SUPERTYPE_TITAN) &&
                   (enchType == ITEM_ENCHANTMENT_TITAN_SLAYING)) {
            totalDmg *= 2;  // double damage vs titan
        }
    }

    // master dagger triple damage backstab
    if (getActualSkillValue(CHARACTER_SKILL_DAGGER).mastery() >= CHARACTER_SKILL_MASTERY_MASTER &&
        pItemTable->pItems[itemId].uSkillType == CHARACTER_SKILL_DAGGER && grng->random(100) < 10)
        totalDmg *= 3;

    return totalDmg;
}

//----- (0048D0B9) --------------------------------------------------------
int Character::GetRangedAttack() {
    int result;
    int weapbonus;
    int skillbonus;

    ItemGen *mainHandItem = GetMainHandItem();

    if (mainHandItem != nullptr &&
        (mainHandItem->uItemID < ITEM_BLASTER ||
         mainHandItem->uItemID > ITEM_BLASTER_RIFLE)) {  // no blasters
        weapbonus = GetItemsBonus(CHARACTER_ATTRIBUTE_RANGED_ATTACK) +
                    GetParameterBonus(GetActualAccuracy());
        skillbonus =
            GetSkillBonus(CHARACTER_ATTRIBUTE_RANGED_ATTACK) + weapbonus;
        result = this->_ranged_atk_bonus +
                 GetMagicalBonus(CHARACTER_ATTRIBUTE_RANGED_ATTACK) +
                 skillbonus;
    } else {
        result = GetActualAttack(true);
    }

    return result;
}

//----- (0048D124) --------------------------------------------------------
int Character::GetRangedDamageMin() {
    int weapbonus = GetItemsBonus(CHARACTER_ATTRIBUTE_RANGED_DMG_MIN);
    int skillbonus =
        GetSkillBonus(CHARACTER_ATTRIBUTE_RANGED_DMG_BONUS) + weapbonus;
    int result = this->_ranged_dmg_bonus +
                 GetMagicalBonus(CHARACTER_ATTRIBUTE_RANGED_DMG_BONUS) +
                 skillbonus;

    if (result < 0)  // cant be less than 0
        result = 0;

    return result;
}

//----- (0048D191) --------------------------------------------------------
int Character::GetRangedDamageMax() {
    int weapbonus = GetItemsBonus(CHARACTER_ATTRIBUTE_RANGED_DMG_MAX);
    int skillbonus =
        GetSkillBonus(CHARACTER_ATTRIBUTE_RANGED_DMG_BONUS) + weapbonus;
    int result = this->_ranged_dmg_bonus +
                 GetMagicalBonus(CHARACTER_ATTRIBUTE_RANGED_DMG_BONUS) +
                 skillbonus;

    if (result < 0) result = 0;

    return result;
}

//----- (0048D1FE) --------------------------------------------------------
int Character::CalculateRangedDamageTo(int uMonsterInfoID) {
    if (!HasItemEquipped(ITEM_SLOT_BOW))  // no bow
        return 0;

    ItemGen *bow =
        (ItemGen*)&this->pInventoryItemList[this->pEquipment.uBow - 1];
    ITEM_ENCHANTMENT itemenchant = bow->special_enchantment;

    signed int dmgperroll = pItemTable->pItems[bow->uItemID].uDamageRoll;
    int damagefromroll = 0;
    int damage = 0;

    damagefromroll = grng->randomDice(pItemTable->pItems[bow->uItemID].uDamageDice, dmgperroll);

    damage = pItemTable->pItems[bow->uItemID].uDamageMod +
             damagefromroll;  // total damage

    if (uMonsterInfoID) {  // check against bow enchantments
        if (itemenchant == 64 &&
            MonsterStats::BelongsToSupertype(
                uMonsterInfoID,
                MONSTER_SUPERTYPE_UNDEAD)) {  // double damage vs undead
            damage *= 2;
        } else if (itemenchant == 39 &&
                   MonsterStats::BelongsToSupertype(
                       uMonsterInfoID,
                       MONSTER_SUPERTYPE_KREEGAN)) {  // double vs devils
            damage *= 2;
        } else if (itemenchant == 40 &&
                   MonsterStats::BelongsToSupertype(
                       uMonsterInfoID,
                       MONSTER_SUPERTYPE_DRAGON)) {  // double vs dragons
            damage *= 2;
        } else if (itemenchant == 63 &&
                   MonsterStats::BelongsToSupertype(
                       uMonsterInfoID,
                       MONSTER_SUPERTYPE_ELF)) {  // double vs elf
            damage *= 2;
        }
    }

    return damage + this->GetSkillBonus(CHARACTER_ATTRIBUTE_RANGED_DMG_BONUS);
}

//----- (0048D2EA) --------------------------------------------------------
std::string Character::GetMeleeDamageString() {
    int min_damage;
    int max_damage;

    ItemGen *mainHandItem = GetMainHandItem();

    if (mainHandItem != nullptr && (mainHandItem->uItemID >= ITEM_WAND_OF_FIRE) &&
        (mainHandItem->uItemID <= ITEM_MYSTIC_WAND_OF_INCINERATION)) {
        return std::string(localization->GetString(LSTR_WAND));
    } else if (mainHandItem != nullptr &&
               (mainHandItem->uItemID == ITEM_BLASTER ||
                mainHandItem->uItemID == ITEM_BLASTER_RIFLE)) {
        min_damage = GetItemsBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_MIN);  // blasters
        max_damage = GetItemsBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_MAX);
    } else {
        min_damage = GetMeleeDamageMinimal();  // weapons
        max_damage = GetMeleeDamageMaximal();
    }

    if (min_damage == max_damage) {
        return fmt::format("{}", min_damage);
    } else {
        return fmt::format("{} - {}", min_damage, max_damage);
    }
}

//----- (0048D396) --------------------------------------------------------
std::string Character::GetRangedDamageString() {
    int min_damage;
    int max_damage;

    ItemGen *mainHandItem = GetMainHandItem();

    if (mainHandItem != nullptr && isWand(mainHandItem->uItemID)) {
        return std::string(localization->GetString(LSTR_WAND));
    } else if (mainHandItem != nullptr &&
               (mainHandItem->uItemID == ITEM_BLASTER ||
                mainHandItem->uItemID == ITEM_BLASTER_RIFLE)) {
        min_damage = GetItemsBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_MIN, true);  // blasters
        max_damage = GetItemsBonus(CHARACTER_ATTRIBUTE_MELEE_DMG_MAX, true);
    } else {
        min_damage = GetRangedDamageMin();  // weaposn
        max_damage = GetRangedDamageMax();
    }

    if (max_damage > 0) {
        if (min_damage == max_damage) {
            return fmt::format("{}", min_damage);
        } else {
            return fmt::format("{} - {}", min_damage, max_damage);
        }
    } else {
        return std::string("N/A");  // no bow equipped
    }
}

//----- (0048D45A) --------------------------------------------------------
bool Character::CanTrainToNextLevel() {
    int lvl = this->uLevel + 1;
    int neededExp = ((lvl * (lvl - 1)) / 2 * 1000);
    return this->experience >= neededExp;
}

//----- (0048D498) --------------------------------------------------------
Color Character::GetExperienceDisplayColor() {
    if (CanTrainToNextLevel())
        return ui_character_bonus_text_color;
    else
        return ui_character_default_text_color;
}

//----- (0048D4B3) --------------------------------------------------------
int Character::CalculateIncommingDamage(DAMAGE_TYPE dmg_type, int dmg) {
    // TODO(captainurist): these are some weird casts to CharacterAttributeType
    if (classType == CHARACTER_CLASS_LICH &&
        ((CharacterAttributeType)dmg_type == CHARACTER_ATTRIBUTE_RESIST_MIND ||
         (CharacterAttributeType)dmg_type == CHARACTER_ATTRIBUTE_RESIST_BODY ||
         (CharacterAttributeType)dmg_type == CHARACTER_ATTRIBUTE_RESIST_SPIRIT))  // TODO(_): determine if spirit
                                                          // resistance should be handled
                                                          // by body res. modifier
        return 0;  // liches are not affected by self magics

    int resist_value = 0;
    switch (dmg_type) {  // get resistance
        case DMGT_FIRE:
            resist_value = GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_FIRE);
            break;
        case DMGT_ELECTR:
            resist_value = GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_AIR);
            break;
        case DMGT_COLD:
            resist_value = GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_WATER);
            break;
        case DMGT_EARTH:
            resist_value = GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_EARTH);
            break;
        case DMGT_SPIRIT:
            resist_value = GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_SPIRIT);
            break;
        case DMGT_MIND:
            resist_value = GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_MIND);
            break;
        case DMGT_BODY:
            resist_value = GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_BODY);
            break;
        default:
            break;
    }

    int player_luck = GetActualLuck();
    signed int res_rand_divider =
        GetParameterBonus(player_luck) + resist_value + 30;

    if (GetParameterBonus(player_luck) + resist_value > 0) {
        for (int i = 0; i < 4; i++) {
            if (grng->random(res_rand_divider) >= 30)
                dmg /= 2;  // damage reduction on successful check
            else
                break;
        }
    }

    ItemGen *equippedArmor = GetArmorItem();
    if ((dmg_type == DMGT_PHISYCAL) &&
        (equippedArmor != nullptr)) {      // physical damage and wearing armour
        if (!equippedArmor->IsBroken()) {  // armour isnt broken
            CharacterSkillType armor_skill = equippedArmor->GetPlayerSkillType();

            // master and above half incoming damage
            if (armor_skill == CHARACTER_SKILL_PLATE) {
                if (getActualSkillValue(CHARACTER_SKILL_PLATE).mastery() >= CHARACTER_SKILL_MASTERY_MASTER)
                    return dmg / 2;
            }

            // grandmaster and chain damage reduce
            if (armor_skill == CHARACTER_SKILL_CHAIN) {
                if (getActualSkillValue(CHARACTER_SKILL_CHAIN).mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER)
                    return dmg * 2 / 3;
            }
        }
    }

    return dmg;
}

//----- (0048D62C) --------------------------------------------------------
ITEM_EQUIP_TYPE Character::GetEquippedItemEquipType(ITEM_SLOT uEquipSlot) const {
    return GetNthEquippedIndexItem(uEquipSlot)->GetItemEquipType();
}

//----- (0048D651) --------------------------------------------------------
CharacterSkillType Character::GetEquippedItemSkillType(ITEM_SLOT uEquipSlot) const {
    return GetNthEquippedIndexItem(uEquipSlot)->GetPlayerSkillType();
}

//----- (0048D676) --------------------------------------------------------
bool Character::IsUnarmed() const {
    return !HasItemEquipped(ITEM_SLOT_MAIN_HAND) &&
           (!HasItemEquipped(ITEM_SLOT_OFF_HAND) ||
            GetOffHandItem()->isShield());
}

//----- (0048D6AA) --------------------------------------------------------
bool Character::HasItemEquipped(ITEM_SLOT uEquipIndex) const {
    uint i = pEquipment.pIndices[uEquipIndex];
    if (i)
        return !pOwnItems[i - 1].IsBroken();
    else
        return false;
}

//----- (0048D6D0) --------------------------------------------------------
bool Character::HasEnchantedItemEquipped(int uEnchantment) const {
    for (ITEM_SLOT i : allItemSlots()) {  // search over equipped inventory
        if (HasItemEquipped(i) &&
            GetNthEquippedIndexItem(i)->special_enchantment == uEnchantment)
            return true;  // check item equipped and is enchanted
    }

    return false;  // no echanted items equipped
}

//----- (0048D709) --------------------------------------------------------
bool Character::WearsItem(ITEM_TYPE item_id, ITEM_SLOT equip_type) const {
    // check aginst specific item and slot
    assert(equip_type != ITEM_SLOT_INVALID && "Invalid item slot passed to WearsItem");
    return (HasItemEquipped(equip_type) && GetNthEquippedIndexItem(equip_type)->uItemID == item_id);
}

bool Character::wearsItemAnywhere(ITEM_TYPE item_id) const {
    for (ITEM_SLOT i : allItemSlots())
        if (WearsItem(item_id, i))
            return true;
    return false;
}

//----- (0048D76C) --------------------------------------------------------
int Character::StealFromShop(
    ItemGen *itemToSteal, int extraStealDifficulty, int reputation,
    int extraStealFine,
    int *fineIfFailed) {  // returns an int, but is the return value is compared
                          // to zero, so might change to bool

    // fineiffailed is changed!

    if (!itemToSteal || !CanAct()) {
        return 0;  // no item or cant act - no stealing
    } else {
        CombinedSkillValue val = this->getActualSkillValue(CHARACTER_SKILL_STEALING);
        unsigned int itemvalue = itemToSteal->GetValue();

        if (itemToSteal->isWeapon())
            itemvalue *= 3;

        int currMaxItemValue = StealingRandomBonuses[grng->random(5)] + val.level() * StealingMasteryBonuses[val.mastery()];
        *fineIfFailed = 100 * (reputation + extraStealDifficulty) + itemvalue;

        if (extraStealFine) {
            *fineIfFailed += 500;
        }

        if (grng->random(100) >= 5) {
            if (*fineIfFailed > currMaxItemValue) {
                if (*fineIfFailed - currMaxItemValue < 500) {
                    return 1;  // fail with item
                } else {
                    return 0;  // failed no item
                }
            } else {
                return 2;  // perfect crime
            }
        } else {
            return 0;  // failed no item
        }
    }
}

//----- (0048D88B) --------------------------------------------------------
int Character::StealFromActor(
    unsigned int uActorID, int _steal_perm,
    int reputation) {  // returns not used - should luck attribute affect

    Actor *actroPtr;
    actroPtr = &pActors[uActorID];

    if (!actroPtr || !CanAct()) {  // no actor to steal from or character cant act
        return STEAL_BUSTED;
    }

    if (!actroPtr->ActorHasItem())  // if actor does not have an item
        actroPtr->SetRandomGoldIfTheresNoItem();  // add some gold

    CombinedSkillValue stealingSkill = this->getActualSkillValue(CHARACTER_SKILL_STEALING);
    int currMaxItemValue = StealingRandomBonuses[grng->random(5)] + stealingSkill.level() * StealingMasteryBonuses[stealingSkill.mastery()];
    int fineIfFailed = actroPtr->monsterInfo.uLevel + 100 * (_steal_perm + reputation);

    if (grng->random(100) < 5 || fineIfFailed > currMaxItemValue ||
        actroPtr->ActorEnemy()) {  // busted
        Actor::AggroSurroundingPeasants(uActorID, 1);
        GameUI_SetStatusBar(LSTR_FMT_S_WAS_CAUGHT_STEALING, this->name);
        return STEAL_BUSTED;
    } else {
        int random = grng->random(100);

        if (random >= 70) {  // stealing gold
            if (!actroPtr->items[3].isGold()) {
                // no gold to steal - fail
                GameUI_SetStatusBar(LSTR_FMT_S_FAILED_TO_STEAL, this->name);
                return STEAL_NOTHING;
            }

            unsigned int enchBonusSum = grng->randomDice(stealingSkill.level(), StealingEnchantmentBonusForSkill[stealingSkill.mastery()]);

            int *enchTypePtr = (int*)&actroPtr->items[3].special_enchantment;  // actor has this amount of gold

            if ((int)enchBonusSum >= *enchTypePtr) {  // steal all the gold
                enchBonusSum = *enchTypePtr;
                actroPtr->items[3].uItemID = ITEM_NULL;
                *enchTypePtr = 0;
            } else {
                *enchTypePtr -= enchBonusSum;  // steal some of the gold
            }

            if (enchBonusSum) {
                pParty->partyFindsGold(enchBonusSum, GOLD_RECEIVE_NOSHARE_SILENT);
                GameUI_SetStatusBar(LSTR_FMT_S_STOLE_D_GOLD, this->name, enchBonusSum);
            } else {
                GameUI_SetStatusBar(LSTR_FMT_S_FAILED_TO_STEAL, this->name);
            }

            return STEAL_SUCCESS;
        } else if (random >= 40) {  // stealing an item
            ItemGen tempItem;
            tempItem.Reset();

            int randslot = grng->random(4);
            ITEM_TYPE carriedItemId = actroPtr->carriedItemId;

            // check if we have an item to steal
            if (carriedItemId != ITEM_NULL || actroPtr->items[randslot].uItemID != ITEM_NULL && !actroPtr->items[randslot].isGold()) {
                if (carriedItemId != ITEM_NULL) {  // load item into tempitem
                    actroPtr->carriedItemId = ITEM_NULL;
                    tempItem.uItemID = carriedItemId;
                    if (pItemTable->pItems[carriedItemId].uEquipType == EQUIP_WAND) {
                        tempItem.uNumCharges = grng->random(6) + pItemTable->pItems[carriedItemId].uDamageMod + 1;
                        tempItem.uMaxCharges = tempItem.uNumCharges;
                    } else if (pItemTable->pItems[carriedItemId].uEquipType == EQUIP_POTION && carriedItemId != ITEM_POTION_BOTTLE) {
                        tempItem.uEnchantmentType = 2 * grng->random(4) + 2;
                    }
                } else {
                    ItemGen *itemToSteal = &actroPtr->items[randslot];
                    memcpy(&tempItem, itemToSteal, sizeof(tempItem));
                    itemToSteal->Reset();
                    carriedItemId = tempItem.uItemID;
                }

                if (carriedItemId != ITEM_NULL) {
                    GameUI_SetStatusBar(LSTR_FMT_S_STOLE_D_ITEM, this->name, pItemTable->pItems[carriedItemId].pUnidentifiedName);
                    pParty->setHoldingItem(&tempItem);
                    return STEAL_SUCCESS;
                }
            }
        }

        GameUI_SetStatusBar(LSTR_FMT_S_FAILED_TO_STEAL, this->name);
        return STEAL_NOTHING;
    }
}

//----- (0048DBB9) --------------------------------------------------------
void Character::Heal(int amount) {
    if (!IsEradicated() && !IsDead()) {  // cant heal
        int max_health = GetMaxHealth();

        if (IsZombie())  // zombie health is halved
            max_health /= 2;

        health += amount;         // add health
        if (health > max_health)  // limits check
            health = max_health;

        if (IsUnconcious()) {
            if (health > 0) {  // wake up if health rises above 0
                SetUnconcious(GameTime(0));
            }
        }
    }
}

int Character::receiveDamage(signed int amount, DAMAGE_TYPE dmg_type) {
    SetAsleep(GameTime(0));  // wake up if asleep
    signed int recieved_dmg = CalculateIncommingDamage(dmg_type, amount);  // get damage
    // for no damage cheat - moved from elsewhere
    if (!engine->config->debug.NoDamage.value()) {
        health -= recieved_dmg;     // reduce health
    }

    if (health < 1) {  // character unconscious or if too hurt - dead
        if ((health + uEndurance + GetItemsBonus(CHARACTER_ATTRIBUTE_ENDURANCE) >= 1) ||
            pCharacterBuffs[CHARACTER_BUFF_PRESERVATION].Active()) {
            SetCondUnconsciousWithBlockCheck(false);
        } else {
            SetCondDeadWithBlockCheck(false);
        }

        if (health <= -10) {  // break armor if health has dropped below -10
            ItemGen *equippedArmor = GetArmorItem();
            if (equippedArmor != nullptr) {  // check there is some armor
                if (!(equippedArmor->uAttributes &
                      ITEM_HARDENED)) {          // if its not hardened
                    equippedArmor->SetBroken();  // break it
                }
            }
        }
    }

    if (recieved_dmg && CanAct()) {
        playReaction(SPEECH_DAMAGED);  // oww
    }

    return recieved_dmg;
}

//----- (0048DCF6) --------------------------------------------------------
int Character::ReceiveSpecialAttackEffect(
    int attType,
    Actor *pActor) {  // long function - consider breaking into two??

    SPECIAL_ATTACK_TYPE attTypeCast = (SPECIAL_ATTACK_TYPE)attType;

    int statcheck;
    int statcheckbonus;
    int luckstat = GetActualLuck();
    signed int itemstobreakcounter = 0;
    char itemstobreaklist[140] {};
    ItemGen *itemtocheck = nullptr;
    ItemGen *itemtobreak = nullptr;
    unsigned int itemtostealinvindex = 0;

    switch (attTypeCast) {
        case SPECIAL_ATTACK_CURSE:
            statcheck = GetActualPersonality();
            statcheckbonus = GetParameterBonus(statcheck);
            break;

        case SPECIAL_ATTACK_WEAK:
        case SPECIAL_ATTACK_SLEEP:
        case SPECIAL_ATTACK_DRUNK:
        case SPECIAL_ATTACK_DISEASE_WEAK:
        case SPECIAL_ATTACK_DISEASE_MEDIUM:
        case SPECIAL_ATTACK_DISEASE_SEVERE:
        case SPECIAL_ATTACK_UNCONSCIOUS:
        case SPECIAL_ATTACK_AGING:
            statcheck = GetActualEndurance();
            statcheckbonus = GetParameterBonus(statcheck);
            break;

        case SPECIAL_ATTACK_INSANE:
        case SPECIAL_ATTACK_PARALYZED:
        case SPECIAL_ATTACK_FEAR:
            statcheckbonus =
                GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_MIND);
            break;

        case SPECIAL_ATTACK_PETRIFIED:
            statcheckbonus =
                GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_EARTH);
            break;

        case SPECIAL_ATTACK_POISON_WEAK:
        case SPECIAL_ATTACK_POISON_MEDIUM:
        case SPECIAL_ATTACK_POISON_SEVERE:
        case SPECIAL_ATTACK_DEAD:
        case SPECIAL_ATTACK_ERADICATED:
            statcheckbonus =
                GetActualResistance(CHARACTER_ATTRIBUTE_RESIST_BODY);
            break;

        case SPECIAL_ATTACK_MANA_DRAIN:
            statcheckbonus = (GetParameterBonus(GetActualIntelligence()) +
                              GetParameterBonus(GetActualPersonality())) /
                             2;
            break;

        case SPECIAL_ATTACK_BREAK_ANY:
            for (int i = 0; i < TOTAL_ITEM_SLOT_COUNT; i++) {
                if (i < INVENTORY_SLOT_COUNT) {
                    itemtocheck = &this->pInventoryItemList[i];
                } else {
                    itemtocheck = &this->pEquippedItems[i - INVENTORY_SLOT_COUNT];
                }

                if (isRegular(itemtocheck->uItemID) && !itemtocheck->IsBroken()) {
                    itemstobreaklist[itemstobreakcounter++] = i;
                }
            }

            if (!itemstobreakcounter) return 0;

            itemtobreak = &this->pInventoryItemList
                               [itemstobreaklist[grng->random(itemstobreakcounter)]];
            statcheckbonus =
                3 * (std::to_underlying(pItemTable->pItems[itemtobreak->uItemID].uMaterial) +
                     itemtobreak->GetDamageMod());
            break;

        case SPECIAL_ATTACK_BREAK_ARMOR:
            for (ITEM_SLOT i : allItemSlots()) {
                if (HasItemEquipped(i)) {
                    if (i == ITEM_SLOT_ARMOUR)
                        itemstobreaklist[itemstobreakcounter++] =
                            this->pEquipment.uArmor - 1;

                    if ((i == ITEM_SLOT_OFF_HAND || i == ITEM_SLOT_MAIN_HAND) &&
                        GetEquippedItemEquipType(i) == EQUIP_SHIELD)
                        itemstobreaklist[itemstobreakcounter++] =
                            this->pEquipment.pIndices[i] - 1;
                }
            }

            if (!itemstobreakcounter) return 0;

            itemtobreak = &this->pInventoryItemList
                               [itemstobreaklist[grng->random(itemstobreakcounter)]];
            statcheckbonus =
                3 * (std::to_underlying(pItemTable->pItems[itemtobreak->uItemID].uMaterial) +
                     itemtobreak->GetDamageMod());
            break;

        case SPECIAL_ATTACK_BREAK_WEAPON:
            for (ITEM_SLOT i : allItemSlots()) {
                if (HasItemEquipped(i)) {
                    if (i == ITEM_SLOT_BOW)
                        itemstobreaklist[itemstobreakcounter++] =
                            (unsigned char)(this->pEquipment.uBow) - 1;

                    if ((i == ITEM_SLOT_OFF_HAND || i == ITEM_SLOT_MAIN_HAND) &&
                        (GetEquippedItemEquipType(i) ==
                             EQUIP_SINGLE_HANDED ||
                         GetEquippedItemEquipType(i) ==
                             EQUIP_TWO_HANDED))
                        itemstobreaklist[itemstobreakcounter++] =
                            this->pEquipment.pIndices[i] - 1;
                }
            }

            if (!itemstobreakcounter) return 0;

            itemtobreak = &this->pInventoryItemList
                               [itemstobreaklist[grng->random(itemstobreakcounter)]];
            statcheckbonus =
                3 * (std::to_underlying(pItemTable->pItems[itemtobreak->uItemID].uMaterial) +
                     itemtobreak->GetDamageMod());
            break;

        case SPECIAL_ATTACK_STEAL:
            for (int i = 0; i < INVENTORY_SLOT_COUNT; i++) {
                int ItemPosInList = this->pInventoryMatrix[i];

                if (ItemPosInList > 0) {
                    itemtocheck = &this->pInventoryItemList[ItemPosInList - 1];

                    if (isRegular(itemtocheck->uItemID)) {
                        itemstobreaklist[itemstobreakcounter++] = i;
                    }
                }
            }

            if (!itemstobreakcounter) return 0;

            itemtostealinvindex =
                itemstobreaklist[grng->random(itemstobreakcounter)];
            statcheck = GetActualAccuracy();
            statcheckbonus = GetParameterBonus(statcheck);
            break;

        default:
            statcheckbonus = 0;
            break;
    }

    signed int savecheck = GetParameterBonus(luckstat) + statcheckbonus + 30;

    if (grng->random(savecheck) >= 30) {  // saving throw if attacke is avoided
        return 0;
    } else {
        int whichplayer = pParty->getCharacterIdInParty(this);

        // pass this to new fucntion??
        // atttypecast - whichplayer - itemtobreak - itemtostealinvindex

        switch (attTypeCast) {
            case SPECIAL_ATTACK_CURSE:
                SetCondition(CONDITION_CURSED, 1);
                pAudioPlayer->playUISound(SOUND_star1);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_WEAK:
                SetCondition(CONDITION_WEAK, 1);
                pAudioPlayer->playUISound(SOUND_star1);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_SLEEP:
                SetCondition(CONDITION_SLEEP, 1);
                pAudioPlayer->playUISound(SOUND_star1);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_DRUNK:
                SetCondition(CONDITION_DRUNK, 1);
                pAudioPlayer->playUISound(SOUND_star1);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_INSANE:
                SetCondition(CONDITION_INSANE, 1);
                pAudioPlayer->playUISound(SOUND_star4);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_POISON_WEAK:
                SetCondition(CONDITION_POISON_WEAK, 1);
                pAudioPlayer->playUISound(SOUND_star2);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_POISON_MEDIUM:
                SetCondition(CONDITION_POISON_MEDIUM, 1);
                pAudioPlayer->playUISound(SOUND_star2);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_POISON_SEVERE:
                SetCondition(CONDITION_POISON_SEVERE, 1);
                pAudioPlayer->playUISound(SOUND_star2);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_DISEASE_WEAK:
                SetCondition(CONDITION_DISEASE_WEAK, 1);
                pAudioPlayer->playUISound(SOUND_star2);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_DISEASE_MEDIUM:
                SetCondition(CONDITION_DISEASE_MEDIUM, 1);
                pAudioPlayer->playUISound(SOUND_star2);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_DISEASE_SEVERE:
                SetCondition(CONDITION_DISEASE_SEVERE, 1);
                pAudioPlayer->playUISound(SOUND_star2);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_PARALYZED:
                SetCondition(CONDITION_PARALYZED, 1);
                pAudioPlayer->playUISound(SOUND_star4);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_UNCONSCIOUS:
                SetCondition(CONDITION_UNCONSCIOUS, 1);
                pAudioPlayer->playUISound(SOUND_star4);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_DEAD:
                SetCondition(CONDITION_DEAD, 1);
                pAudioPlayer->playUISound(SOUND_eradicate);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_PETRIFIED:
                SetCondition(CONDITION_PETRIFIED, 1);
                pAudioPlayer->playUISound(SOUND_eradicate);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_ERADICATED:
                SetCondition(CONDITION_ERADICATED, 1);
                pAudioPlayer->playUISound(SOUND_eradicate);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_BREAK_ANY:
            case SPECIAL_ATTACK_BREAK_ARMOR:
            case SPECIAL_ATTACK_BREAK_WEAPON:
                if (!(itemtobreak->uAttributes & ITEM_HARDENED)) {
                    playReaction(SPEECH_ITEM_BROKEN);
                    itemtobreak->SetBroken();
                    pAudioPlayer->playUISound(SOUND_metal_vs_metal03h);
                }
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_STEAL: {
                playReaction(SPEECH_ITEM_BROKEN);
                void *actoritems = &pActor->items[0];
                if (pActor->items[0].uItemID != ITEM_NULL) {
                    actoritems = &pActor->items[1];
                    if (pActor->items[1].uItemID != ITEM_NULL) {
                        spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                        return 1;
                    }
                }

                memcpy(actoritems,
                       &this->pInventoryItemList[this->pInventoryMatrix[itemtostealinvindex] - 1],
                       0x24u);
                RemoveItemAtInventoryIndex(itemtostealinvindex);
                pAudioPlayer->playUISound(SOUND_metal_vs_metal03h);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;
            }

            case SPECIAL_ATTACK_AGING:
                playReaction(SPEECH_AGING);
                ++this->sAgeModifier;
                pAudioPlayer->playUISound(SOUND_eleccircle);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_MANA_DRAIN:
                playReaction(SPEECH_SP_DRAINED);
                this->mana = 0;
                pAudioPlayer->playUISound(SOUND_eleccircle);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            case SPECIAL_ATTACK_FEAR:
                SetCondition(CONDITION_FEAR, 1);
                pAudioPlayer->playUISound(SOUND_star1);
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_DISEASE, whichplayer);
                return 1;
                break;

            default:
                return 0;
        }
    }
}

// 48DCF6: using guessed type char var_94[140];

//----- (0048E1A3) --------------------------------------------------------
unsigned int Character::GetSpellSchool(SPELL_TYPE uSpellID) const {
    return pSpellStats->pInfos[uSpellID].uSchool;
}

//----- (0048E1B5) --------------------------------------------------------
int Character::GetAttackRecoveryTime(bool bRangedAttack) const {
    const ItemGen *weapon = nullptr;
    uint weapon_recovery = base_recovery_times_per_weapon_type[CHARACTER_SKILL_STAFF];
    if (bRangedAttack) {
        if (HasItemEquipped(ITEM_SLOT_BOW)) {
            weapon = GetBowItem();
            weapon_recovery = base_recovery_times_per_weapon_type[weapon->GetPlayerSkillType()];
        }
    } else if (IsUnarmed() && getActualSkillValue(CHARACTER_SKILL_UNARMED).level() > 0) {
        // TODO(captainurist): just set unarmed recovery properly in the table.
        weapon_recovery = base_recovery_times_per_weapon_type[CHARACTER_SKILL_DAGGER];
    } else if (HasItemEquipped(ITEM_SLOT_MAIN_HAND)) {
        weapon = GetMainHandItem();
        if (weapon->isWand()) {
            weapon_recovery = pSpellDatas[wandSpellIds[weapon->uItemID]].uExpertLevelRecovery;
        } else {
            weapon_recovery = base_recovery_times_per_weapon_type[weapon->GetPlayerSkillType()];
        }
    }

    uint shield_recovery = 0;
    if (HasItemEquipped(ITEM_SLOT_OFF_HAND)) {
        if (GetEquippedItemEquipType(ITEM_SLOT_OFF_HAND) == EQUIP_SHIELD) {
            CharacterSkillType skill_type = GetOffHandItem()->GetPlayerSkillType();
            uint shield_base_recovery = base_recovery_times_per_weapon_type[skill_type];
            float multiplier = GetArmorRecoveryMultiplierFromSkillLevel(skill_type, 1.0f, 0, 0, 0);
            shield_recovery = (uint)(shield_base_recovery * multiplier);
        } else {
            if (base_recovery_times_per_weapon_type[GetOffHandItem()->GetPlayerSkillType()] > weapon_recovery) {
                weapon = GetOffHandItem();
                weapon_recovery = base_recovery_times_per_weapon_type[weapon->GetPlayerSkillType()];
            }
        }
    }

    uint armour_recovery = 0;
    if (HasItemEquipped(ITEM_SLOT_ARMOUR)) {
        CharacterSkillType armour_skill_type = GetArmorItem()->GetPlayerSkillType();
        uint base_armour_recovery = base_recovery_times_per_weapon_type[armour_skill_type];
        float multiplier;

        if (armour_skill_type == CHARACTER_SKILL_LEATHER) {
            multiplier = GetArmorRecoveryMultiplierFromSkillLevel(armour_skill_type, 1.0f, 0, 0, 0);
        } else if (armour_skill_type == CHARACTER_SKILL_CHAIN) {
            multiplier = GetArmorRecoveryMultiplierFromSkillLevel(armour_skill_type, 1.0f, 0.5f, 0, 0);
        } else if (armour_skill_type == CHARACTER_SKILL_PLATE) {
            multiplier = GetArmorRecoveryMultiplierFromSkillLevel(armour_skill_type, 1.0f, 0.5f, 0.5f, 0);
        } else {
            assert(armour_skill_type == CHARACTER_SKILL_MISC && GetArmorItem()->uItemID == ITEM_QUEST_WETSUIT);
            multiplier = GetArmorRecoveryMultiplierFromSkillLevel(armour_skill_type, 1.0f, 1.0f, 1.0f, 1.0f);
        }

        armour_recovery = (uint)(base_armour_recovery * multiplier);
    }

    uint player_speed_recovery_reduction = GetParameterBonus(GetActualSpeed());

    uint sword_axe_bow_recovery_reduction = 0;
    if (weapon != nullptr) {
        CombinedSkillValue weaponSkill = getActualSkillValue(weapon->GetPlayerSkillType());
        if (weaponSkill.level() > 0 &&
            (weapon->GetPlayerSkillType() == CHARACTER_SKILL_SWORD ||
             weapon->GetPlayerSkillType() == CHARACTER_SKILL_AXE ||
             weapon->GetPlayerSkillType() == CHARACTER_SKILL_BOW)) {
            // Expert Sword, Axe & Bow reduce recovery
            if (weaponSkill.mastery() >= CHARACTER_SKILL_MASTERY_EXPERT)
                sword_axe_bow_recovery_reduction = weaponSkill.level();
        }
    }

    bool shooting_laser = weapon && weapon->GetPlayerSkillType() == CHARACTER_SKILL_BLASTER;
    Assert(shooting_laser ? !bRangedAttack : true); // For blasters we expect bRangedAttack == false.

    uint armsmaster_recovery_reduction = 0;
    if (!bRangedAttack && !shooting_laser) {
        CombinedSkillValue armsmasterSkill = getActualSkillValue(CHARACTER_SKILL_ARMSMASTER);
        if (armsmasterSkill.level() > 0) {
            armsmaster_recovery_reduction = armsmasterSkill.level();
            if (armsmasterSkill.mastery() >= CHARACTER_SKILL_MASTERY_GRANDMASTER)
                armsmaster_recovery_reduction *= 2;
        }
    }

    uint hasteRecoveryReduction = 0;
    if (pCharacterBuffs[CHARACTER_BUFF_HASTE].Active()) hasteRecoveryReduction = 25;
    if (pParty->pPartyBuffs[PARTY_BUFF_HASTE].Active()) hasteRecoveryReduction = 25;

    uint weapon_enchantment_recovery_reduction = 0;
    if (weapon != nullptr) {
        if (weapon->special_enchantment == ITEM_ENCHANTMENT_SWIFT ||
            weapon->special_enchantment == ITEM_ENCHANTMENT_OF_DARKNESS ||
            weapon->uItemID == ITEM_ARTIFACT_PUCK)
            weapon_enchantment_recovery_reduction = 20;
    }

    int recovery = weapon_recovery + armour_recovery + shield_recovery -
                   armsmaster_recovery_reduction -
                   weapon_enchantment_recovery_reduction -
                   hasteRecoveryReduction - sword_axe_bow_recovery_reduction -
                   player_speed_recovery_reduction;

    uint minRecovery;
    if (shooting_laser) {
        minRecovery = engine->config->gameplay.MinRecoveryBlasters.value();
    } else if (bRangedAttack) {
        minRecovery = engine->config->gameplay.MinRecoveryRanged.value();
    } else {
        minRecovery = engine->config->gameplay.MinRecoveryMelee.value();
    }

    if (recovery < minRecovery)
        recovery = minRecovery;

    return recovery;
}

//----- new --------------------------------------------------------
float Character::GetArmorRecoveryMultiplierFromSkillLevel(CharacterSkillType armour_skill_type, float mult1, float mult2, float mult3, float mult4) const {
    CharacterSkillMastery skillMastery = getSkillValue(armour_skill_type).mastery();

    switch (skillMastery) {
        case CHARACTER_SKILL_MASTERY_NOVICE:
            return mult1;
            break;
        case CHARACTER_SKILL_MASTERY_EXPERT:
            return mult2;
            break;
        case CHARACTER_SKILL_MASTERY_MASTER:
            return mult3;
            break;
        case CHARACTER_SKILL_MASTERY_GRANDMASTER:
            return mult4;
            break;
        default:
            break;
    }

    Error("Unexpected input value: %d", armour_skill_type);
    return 0;
}

//----- (0048E4F8) --------------------------------------------------------
int Character::GetMaxHealth() const {
    int endbonus = GetParameterBonus(GetActualEndurance());
    int healthbylevel =
        pBaseHealthPerLevelByClass[classType] * (GetActualLevel() + endbonus);
    int itembonus = GetItemsBonus(CHARACTER_ATTRIBUTE_HEALTH) + healthbylevel;
    int maxhealth = uFullHealthBonus + pBaseHealthByClass[classType / 4] +
                    GetSkillBonus(CHARACTER_ATTRIBUTE_HEALTH) + itembonus;

    if (maxhealth < 0)  // min zero
        maxhealth = 0;

    return maxhealth;
}

//----- (0048E565) --------------------------------------------------------
int Character::GetMaxMana() const {
    int mainmanastat;
    int statbonus;
    int addmanastat;

    switch (classType) {
        case CHARACTER_CLASS_ROGUE:
        case CHARACTER_CLASS_SPY:
        case CHARACTER_CLASS_ASSASSIN:
        case CHARACTER_CLASS_ARCHER:
        case CHARACTER_CLASS_WARRIOR_MAGE:
        case CHARACTER_CLASS_MASTER_ARCHER:
        case CHARACTER_CLASS_SNIPER:
        case CHARACTER_CLASS_SORCERER:
        case CHARACTER_CLASS_WIZARD:
        case CHARACTER_CLASS_ARCHAMGE:
        case CHARACTER_CLASS_LICH:
            // intelligence based mana
            mainmanastat = GetActualIntelligence();
            statbonus = GetParameterBonus(mainmanastat);
            break;

        case CHARACTER_CLASS_INITIATE:
        case CHARACTER_CLASS_MASTER:
        case CHARACTER_CLASS_NINJA:
        case CHARACTER_CLASS_PALADIN:
        case CHARACTER_CLASS_CRUSADER:
        case CHARACTER_CLASS_HERO:
        case CHARACTER_CLASS_VILLIAN:
        case CHARACTER_CLASS_CLERIC:
        case CHARACTER_CLASS_PRIEST:
        case CHARACTER_CLASS_PRIEST_OF_SUN:
        case CHARACTER_CLASS_PRIEST_OF_MOON:
            // personality based mana
            mainmanastat = GetActualPersonality();
            statbonus = GetParameterBonus(mainmanastat);
            break;

        case CHARACTER_CLASS_HUNTER:
        case CHARACTER_CLASS_RANGER_LORD:
        case CHARACTER_CLASS_BOUNTY_HUNTER:
        case CHARACTER_CLASS_DRUID:
        case CHARACTER_CLASS_GREAT_DRUID:
        case CHARACTER_CLASS_ARCH_DRUID:
        case CHARACTER_CLASS_WARLOCK:
            // mixed base mana
            mainmanastat = GetActualPersonality();
            statbonus = GetParameterBonus(mainmanastat);
            addmanastat = GetActualIntelligence();
            statbonus += GetParameterBonus(addmanastat);
            break;

        default:  // no magic
            return 0;
            break;
    }

    int manabylevel =
        pBaseManaPerLevelByClass[classType] * (GetActualLevel() + statbonus);
    int itembonus = GetItemsBonus(CHARACTER_ATTRIBUTE_MANA) + manabylevel;
    int maxmana = uFullManaBonus + pBaseManaByClass[classType / 4] +
                  GetSkillBonus(CHARACTER_ATTRIBUTE_MANA) + itembonus;

    if (maxmana < 0)  // min of 0
        maxmana = 0;

    return maxmana;
}

//----- (0048E656) --------------------------------------------------------
int Character::GetBaseAC() const {
    int spd = GetActualSpeed();
    int spdbonus = GetParameterBonus(spd);
    int itembonus = GetItemsBonus(CHARACTER_ATTRIBUTE_AC_BONUS) + spdbonus;
    int skillbonus = GetSkillBonus(CHARACTER_ATTRIBUTE_AC_BONUS) + itembonus;

    if (skillbonus < 0)  // min zero
        skillbonus = 0;

    return skillbonus;
}

//----- (0048E68F) --------------------------------------------------------
int Character::GetActualAC() const {
    int spd = GetActualSpeed();
    int spdbonus = GetParameterBonus(spd);
    int itembonus = GetItemsBonus(CHARACTER_ATTRIBUTE_AC_BONUS) + spdbonus;
    int skillbonus = GetSkillBonus(CHARACTER_ATTRIBUTE_AC_BONUS) + itembonus;

    int result = this->sACModifier + GetMagicalBonus(CHARACTER_ATTRIBUTE_AC_BONUS) + skillbonus;

    if (result < 0)  // min zero
        result = 0;

    return result;
}

//----- (0048E6DC) --------------------------------------------------------
unsigned int Character::GetBaseAge() const {
    return pParty->GetPlayingTime().GetYears() - this->uBirthYear + game_starting_year;
}

//----- (0048E72C) --------------------------------------------------------
unsigned int Character::GetActualAge() const {
    return this->sAgeModifier + GetBaseAge();
}

//----- (0048E73F) --------------------------------------------------------
int Character::GetBaseResistance(CharacterAttributeType a2) const {
    int v7;  // esi@20
    int racialBonus = 0;
    const int16_t *resStat;
    int result;

    switch (a2) {
        case CHARACTER_ATTRIBUTE_RESIST_FIRE:
            resStat = &sResFireBase;
            if (IsRaceGoblin()) racialBonus = 5;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_AIR:
            resStat = &sResAirBase;
            if (IsRaceGoblin()) racialBonus = 5;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_WATER:
            resStat = &sResWaterBase;
            if (IsRaceDwarf()) racialBonus = 5;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_EARTH:
            resStat = &sResEarthBase;
            if (IsRaceDwarf()) racialBonus = 5;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_MIND:
            resStat = &sResMindBase;
            if (IsRaceElf()) racialBonus = 10;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_BODY:
        case CHARACTER_ATTRIBUTE_RESIST_SPIRIT:
            resStat = &sResBodyBase;
            if (IsRaceHuman()) racialBonus = 5;
            break;
        default:
            Error("Unknown attribute");
    }
    v7 = GetItemsBonus(a2) + racialBonus;
    result = v7 + *resStat;
    if (classType == CHARACTER_CLASS_LICH) {
        if (result > 200) result = 200;
    }
    return result;
}

//----- (0048E7D0) --------------------------------------------------------
int Character::GetActualResistance(CharacterAttributeType resistance) const {
    signed int v10 = 0;  // [sp+14h] [bp-4h]@1
    const int16_t *resStat;
    int result;
    int baseRes;

    CombinedSkillValue leatherSkill = getActualSkillValue(CHARACTER_SKILL_LEATHER);

    if (CheckHiredNPCSpeciality(Enchanter)) v10 = 20;
    if ((resistance == CHARACTER_ATTRIBUTE_RESIST_FIRE ||
         resistance == CHARACTER_ATTRIBUTE_RESIST_AIR ||
         resistance == CHARACTER_ATTRIBUTE_RESIST_WATER ||
         resistance == CHARACTER_ATTRIBUTE_RESIST_EARTH) &&
        leatherSkill.mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER &&
        HasItemEquipped(ITEM_SLOT_ARMOUR) &&
        GetEquippedItemSkillType(ITEM_SLOT_ARMOUR) == CHARACTER_SKILL_LEATHER)
        v10 += leatherSkill.level();

    switch (resistance) {
        case CHARACTER_ATTRIBUTE_RESIST_FIRE:
            resStat = &sResFireBonus;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_AIR:
            resStat = &sResAirBonus;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_WATER:
            resStat = &sResWaterBonus;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_EARTH:
            resStat = &sResEarthBonus;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_MIND:
            resStat = &sResMindBonus;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_BODY:
        case CHARACTER_ATTRIBUTE_RESIST_SPIRIT:
            resStat = &sResBodyBonus;
            break;
        default:
            Error("Unexpected attribute");
    }
    baseRes = GetBaseResistance(resistance);
    result = v10 + GetMagicalBonus(resistance) + baseRes + *(resStat);
    if (classType == CHARACTER_CLASS_LICH) {
        if (result > 200) result = 200;
    }
    return result;
}

//----- (0048E8F5) --------------------------------------------------------
bool Character::Recover(GameTime dt) {
    int timepassed =
        dt.value * GetSpecialItemBonus(ITEM_ENCHANTMENT_OF_RECOVERY) * 0.01 + dt.value;

    if (timeToRecovery > timepassed) {  // need more time till recovery
        timeToRecovery -= timepassed;
        return true;
    } else {
        timeToRecovery = 0;  // recovered

        if (!pParty->hasActiveCharacter())  // set recoverd char as active
            pParty->switchToNextActiveCharacter();

        return false;
    }
}

//----- (0048E96A) --------------------------------------------------------
void Character::SetRecoveryTime(signed int rec) {
    // to avoid switching characters if endurance eliminates hit recovery
    if (rec < 1) return;

    if (rec > timeToRecovery) timeToRecovery = rec;

    if (pParty->hasActiveCharacter() && &pParty->activeCharacter() == this &&
        !enchantingActiveCharacter)
        pParty->switchToNextActiveCharacter();
}

//----- (0048E9B7) --------------------------------------------------------
void Character::RandomizeName() {
    if (!uExpressionTimePassed)
        name = pNPCStats->pNPCNames[grng->random(pNPCStats->uNumNPCNames[uSex])][uSex];
}

//----- (0048E9F4) --------------------------------------------------------
Condition Character::GetMajorConditionIdx() const {
    for (Condition condition : conditionImportancyTable()) {
        if (conditions.Has(condition))
            return condition;  // return worst condition
    }
    return CONDITION_GOOD;  // condition good
}

//----- (0048EA1B) --------------------------------------------------------
int Character::GetParameterBonus(int player_parameter) const {
    int i;  // eax@1
    i = 0;
    while (param_to_bonus_table[i]) {
        if (player_parameter >= param_to_bonus_table[i]) break;
        ++i;
    }
    return parameter_to_bonus_value[i];
}

//----- (0048EA46) --------------------------------------------------------
int Character::GetSpecialItemBonus(ITEM_ENCHANTMENT enchantment) const {
    for (ITEM_SLOT i : allItemSlots()) {
        if (HasItemEquipped(i)) {
            if (enchantment == ITEM_ENCHANTMENT_OF_RECOVERY) {
                if (GetNthEquippedIndexItem(i)->special_enchantment ==
                        ITEM_ENCHANTMENT_OF_RECOVERY ||
                    (GetNthEquippedIndexItem(i)->uItemID ==
                     ITEM_ARTIFACT_ELVEN_CHAINMAIL))
                    return 50;
            }
            if (enchantment == ITEM_ENCHANTMENT_OF_FORCE) {
                if (GetNthEquippedIndexItem(i)->special_enchantment ==
                    ITEM_ENCHANTMENT_OF_FORCE)
                    return 5;
            }
        }
    }
    return 0;
}

//----- (0048EAAE) --------------------------------------------------------
int Character::GetItemsBonus(CharacterAttributeType attr, bool getOnlyMainHandDmg /*= false*/) const {
    int v5;                     // edi@1
    int v14;                    // ecx@58
    int v15;                    // eax@58
    int v25;                    // ecx@80
    int v26;                    // edi@80
    int v56;                    // eax@365
    CharacterSkillType v58;             // [sp-4h] [bp-20h]@10
    int v61;                    // [sp+10h] [bp-Ch]@1
    int v62;                    // [sp+14h] [bp-8h]@1
    const ItemGen *currEquippedItem;  // [sp+20h] [bp+4h]@101
    bool no_skills;

    v5 = 0;
    v62 = 0;
    v61 = 0;

    no_skills = false;
    switch (attr) {
        case CHARACTER_ATTRIBUTE_SKILL_ALCHEMY:
            v58 = CHARACTER_SKILL_ALCHEMY;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_STEALING:
            v58 = CHARACTER_SKILL_STEALING;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_TRAP_DISARM:
            v58 = CHARACTER_SKILL_TRAP_DISARM;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_ITEM_ID:
            v58 = CHARACTER_SKILL_ITEM_ID;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_MONSTER_ID:
            v58 = CHARACTER_SKILL_MONSTER_ID;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_ARMSMASTER:
            v58 = CHARACTER_SKILL_ARMSMASTER;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_DODGE:
            v58 = CHARACTER_SKILL_DODGE;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_UNARMED:
            v58 = CHARACTER_SKILL_UNARMED;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_FIRE:
            v58 = CHARACTER_SKILL_FIRE;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_AIR:
            v58 = CHARACTER_SKILL_AIR;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_WATER:
            v58 = CHARACTER_SKILL_WATER;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_EARTH:
            v58 = CHARACTER_SKILL_EARTH;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_SPIRIT:
            v58 = CHARACTER_SKILL_SPIRIT;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_MIND:
            v58 = CHARACTER_SKILL_MIND;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_BODY:
            v58 = CHARACTER_SKILL_BODY;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_LIGHT:
            v58 = CHARACTER_SKILL_LIGHT;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_DARK:
            v58 = CHARACTER_SKILL_DARK;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_MEDITATION:
            v58 = CHARACTER_SKILL_MEDITATION;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_BOW:
            v58 = CHARACTER_SKILL_BOW;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_SHIELD:
            v58 = CHARACTER_SKILL_SHIELD;
            break;
        case CHARACTER_ATTRIBUTE_SKILL_LEARNING:
            v58 = CHARACTER_SKILL_LEARNING;
            break;
        default:
            no_skills = true;
    }
    if (!no_skills) {
        if (!this->pActiveSkills[v58]) return 0;
    }

    switch (attr) {  // TODO(_) would be nice to move these into separate functions
        case CHARACTER_ATTRIBUTE_RANGED_DMG_BONUS:
        case CHARACTER_ATTRIBUTE_RANGED_ATTACK:
            if (HasItemEquipped(ITEM_SLOT_BOW)) v5 = GetBowItem()->GetDamageMod();
            return v5;
            break;

        case CHARACTER_ATTRIBUTE_RANGED_DMG_MIN:
            if (!HasItemEquipped(ITEM_SLOT_BOW)) return 0;
            v5 = GetBowItem()->GetDamageMod();
            v56 = GetBowItem()->GetDamageDice();
            return v5 + v56;
            break;

        case CHARACTER_ATTRIBUTE_RANGED_DMG_MAX:
            if (!HasItemEquipped(ITEM_SLOT_BOW)) return 0;
            v5 = GetBowItem()->GetDamageDice() * GetBowItem()->GetDamageRoll();
            v56 = GetBowItem()->GetDamageMod();
            return v5 + v56;

        case CHARACTER_ATTRIBUTE_LEVEL:
            if (!Character::HasEnchantedItemEquipped(ITEM_ENCHANTMENT_OF_POWER)) return 0;
            return 5;
            break;

        case CHARACTER_ATTRIBUTE_MELEE_DMG_MAX:
            if (IsUnarmed()) {
                return 3;
            } else {
                if (this->HasItemEquipped(ITEM_SLOT_MAIN_HAND)) {
                    if (isWeapon(GetEquippedItemEquipType(ITEM_SLOT_MAIN_HAND))) {
                        const ItemGen *mainHandItem = GetMainHandItem();
                        v26 = mainHandItem->GetDamageRoll();
                        if (GetOffHandItem() != nullptr ||
                            mainHandItem->GetPlayerSkillType() != CHARACTER_SKILL_SPEAR) {
                            v25 = mainHandItem->GetDamageDice();
                        } else {
                            v25 = mainHandItem->GetDamageDice() + 1;
                        }
                        v5 = mainHandItem->GetDamageMod() + v25 * v26;
                    }
                }
                if (getOnlyMainHandDmg ||
                    !this->HasItemEquipped(ITEM_SLOT_OFF_HAND) ||
                    !isWeapon(GetEquippedItemEquipType(ITEM_SLOT_OFF_HAND))) {
                    return v5;
                } else {
                    const ItemGen *offHandItem = GetOffHandItem();
                    v15 = offHandItem->GetDamageMod();
                    v14 = offHandItem->GetDamageDice() *
                          offHandItem->GetDamageRoll();
                    return v5 + v15 + v14;
                }
            }
            break;

        case CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS:
        case CHARACTER_ATTRIBUTE_ATTACK:
            if (IsUnarmed()) {
                return 0;
            }
            if (this->HasItemEquipped(ITEM_SLOT_MAIN_HAND)) {
                if (isWeapon(GetEquippedItemEquipType(ITEM_SLOT_MAIN_HAND))) {
                    v5 = GetMainHandItem()->GetDamageMod();
                }
            }
            if (getOnlyMainHandDmg ||
                !this->HasItemEquipped(ITEM_SLOT_OFF_HAND) ||
                !isWeapon(this->GetEquippedItemEquipType(ITEM_SLOT_OFF_HAND))) {
                return v5;
            } else {
                v56 = GetOffHandItem()->GetDamageMod();
                return v5 + v56;
            }
            break;

        case CHARACTER_ATTRIBUTE_MELEE_DMG_MIN:
            if (IsUnarmed()) {
                return 1;
            }
            if (this->HasItemEquipped(ITEM_SLOT_MAIN_HAND)) {
                if (isWeapon(GetEquippedItemEquipType(ITEM_SLOT_MAIN_HAND))) {
                    const ItemGen *mainHandItem = GetMainHandItem();
                    v5 = mainHandItem->GetDamageDice() +
                         mainHandItem->GetDamageMod();
                    if (GetOffHandItem() == nullptr &&
                        mainHandItem->GetPlayerSkillType() == CHARACTER_SKILL_SPEAR) {
                        ++v5;
                    }
                }
            }

            if (getOnlyMainHandDmg ||
                !this->HasItemEquipped(ITEM_SLOT_OFF_HAND) ||
                !isWeapon(GetEquippedItemEquipType(ITEM_SLOT_OFF_HAND))) {
                return v5;
            } else {
                const ItemGen *offHandItem = GetOffHandItem();
                v14 = offHandItem->GetDamageMod();
                v15 = offHandItem->GetDamageDice();
                return v5 + v15 + v14;
            }
            break;

        case CHARACTER_ATTRIBUTE_MIGHT:
        case CHARACTER_ATTRIBUTE_INTELLIGENCE:
        case CHARACTER_ATTRIBUTE_PERSONALITY:
        case CHARACTER_ATTRIBUTE_ENDURANCE:
        case CHARACTER_ATTRIBUTE_ACCURACY:
        case CHARACTER_ATTRIBUTE_SPEED:
        case CHARACTER_ATTRIBUTE_LUCK:
        case CHARACTER_ATTRIBUTE_HEALTH:
        case CHARACTER_ATTRIBUTE_MANA:
        case CHARACTER_ATTRIBUTE_AC_BONUS:

        case CHARACTER_ATTRIBUTE_RESIST_FIRE:
        case CHARACTER_ATTRIBUTE_RESIST_AIR:
        case CHARACTER_ATTRIBUTE_RESIST_WATER:
        case CHARACTER_ATTRIBUTE_RESIST_EARTH:
        case CHARACTER_ATTRIBUTE_RESIST_MIND:
        case CHARACTER_ATTRIBUTE_RESIST_BODY:
        case CHARACTER_ATTRIBUTE_RESIST_SPIRIT:

        case CHARACTER_ATTRIBUTE_SKILL_ALCHEMY:
        case CHARACTER_ATTRIBUTE_SKILL_STEALING:
        case CHARACTER_ATTRIBUTE_SKILL_TRAP_DISARM:
        case CHARACTER_ATTRIBUTE_SKILL_ITEM_ID:
        case CHARACTER_ATTRIBUTE_SKILL_MONSTER_ID:
        case CHARACTER_ATTRIBUTE_SKILL_ARMSMASTER:
        case CHARACTER_ATTRIBUTE_SKILL_DODGE:
        case CHARACTER_ATTRIBUTE_SKILL_UNARMED:

        case CHARACTER_ATTRIBUTE_SKILL_FIRE:
        case CHARACTER_ATTRIBUTE_SKILL_AIR:
        case CHARACTER_ATTRIBUTE_SKILL_WATER:
        case CHARACTER_ATTRIBUTE_SKILL_EARTH:
        case CHARACTER_ATTRIBUTE_SKILL_SPIRIT:
        case CHARACTER_ATTRIBUTE_SKILL_MIND:
        case CHARACTER_ATTRIBUTE_SKILL_BODY:
        case CHARACTER_ATTRIBUTE_SKILL_LIGHT:
        case CHARACTER_ATTRIBUTE_SKILL_DARK:
        case CHARACTER_ATTRIBUTE_SKILL_MEDITATION:
        case CHARACTER_ATTRIBUTE_SKILL_BOW:
        case CHARACTER_ATTRIBUTE_SKILL_SHIELD:
        case CHARACTER_ATTRIBUTE_SKILL_LEARNING:
            for (ITEM_SLOT i : allItemSlots()) {
                if (HasItemEquipped(i)) {
                    currEquippedItem = GetNthEquippedIndexItem(i);
                    if (attr == CHARACTER_ATTRIBUTE_AC_BONUS) {
                        if (isPassiveEquipment(currEquippedItem->GetItemEquipType())) {
                            v5 += currEquippedItem->GetDamageDice() +
                                  currEquippedItem->GetDamageMod();
                        }
                    }
                    if (pItemTable->IsMaterialNonCommon(currEquippedItem) &&
                        !pItemTable->IsMaterialSpecial(currEquippedItem)) {
                        currEquippedItem->GetItemBonusArtifact(this, attr,
                                                               &v62);
                    } else if (currEquippedItem->uEnchantmentType != 0) {
                        if (this->pInventoryItemList
                                    [this->pEquipment.pIndices[i] - 1]
                                        .uEnchantmentType -
                                1 == attr) {  // if
                                   // (currEquippedItem->IsRegularEnchanmentForAttribute(attr))
                            if (attr > CHARACTER_ATTRIBUTE_RESIST_BODY &&
                                v5 < currEquippedItem
                                         ->m_enchantmentStrength)  // for skills
                                                                   // bonuses
                                v5 = currEquippedItem->m_enchantmentStrength;
                            else  // for resists and attributes bonuses
                                v5 += currEquippedItem->m_enchantmentStrength;
                        }
                    } else {
                        currEquippedItem->GetItemBonusSpecialEnchantment(this, attr, &v5, &v61);
                    }
                }
            }
            return v5 + v62 + v61;
            break;
        default:
            return 0;
    }
}

//----- (0048F73C) --------------------------------------------------------
int Character::GetMagicalBonus(CharacterAttributeType a2) const {
    int v3 = 0;  // eax@4
    int v4 = 0;  // ecx@5

    switch (a2) {
        case CHARACTER_ATTRIBUTE_RESIST_FIRE:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_RESIST_FIRE].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_RESIST_FIRE].power;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_AIR:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_RESIST_AIR].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_RESIST_AIR].power;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_BODY:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_RESIST_BODY].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_RESIST_BODY].power;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_WATER:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_RESIST_WATER].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_RESIST_WATER].power;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_EARTH:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_RESIST_EARTH].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_RESIST_EARTH].power;
            break;
        case CHARACTER_ATTRIBUTE_RESIST_MIND:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_RESIST_MIND].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_RESIST_MIND].power;
            break;
        case CHARACTER_ATTRIBUTE_ATTACK:
        case CHARACTER_ATTRIBUTE_RANGED_ATTACK:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_BLESS]
                     .power;  // only character effect spell in both VI and VII
            break;
        case CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_HEROISM].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_HEROISM].power;
            break;
        case CHARACTER_ATTRIBUTE_MIGHT:
            v3 = pCharacterBuffs[CHARACTER_BUFF_STRENGTH].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_DAY_OF_GODS].power;
            break;
        case CHARACTER_ATTRIBUTE_INTELLIGENCE:
            v3 = pCharacterBuffs[CHARACTER_BUFF_INTELLIGENCE].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_DAY_OF_GODS].power;
            break;
        case CHARACTER_ATTRIBUTE_PERSONALITY:
            v3 = pCharacterBuffs[CHARACTER_BUFF_PERSONALITY].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_DAY_OF_GODS].power;
            break;
        case CHARACTER_ATTRIBUTE_ENDURANCE:
            v3 = pCharacterBuffs[CHARACTER_BUFF_ENDURANCE].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_DAY_OF_GODS].power;
            break;
        case CHARACTER_ATTRIBUTE_ACCURACY:
            v3 = pCharacterBuffs[CHARACTER_BUFF_ACCURACY].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_DAY_OF_GODS].power;
            break;
        case CHARACTER_ATTRIBUTE_SPEED:
            v3 = pCharacterBuffs[CHARACTER_BUFF_SPEED].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_DAY_OF_GODS].power;
            break;
        case CHARACTER_ATTRIBUTE_LUCK:
            v3 = pCharacterBuffs[CHARACTER_BUFF_LUCK].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_DAY_OF_GODS].power;
            break;
        case CHARACTER_ATTRIBUTE_AC_BONUS:
            v3 = this->pCharacterBuffs[CHARACTER_BUFF_STONESKIN].power;
            v4 = pParty->pPartyBuffs[PARTY_BUFF_STONE_SKIN].power;
            break;
        default:
            break;
    }
    return v3 + v4;
}

//----- (0048F882) --------------------------------------------------------
int Character::actualSkillLevel(CharacterSkillType skill) const {
    if (skill == CHARACTER_SKILL_CLUB && engine->config->gameplay.TreatClubAsMace.value()) {
        // some items loaded in as clubs
        skill = CHARACTER_SKILL_MACE;
    }

    // Vanilla returned 0 for CHARACTER_SKILL_MISC here, we return 1.
    int base = getSkillValue(skill).level();

    int bonus = 0;
    switch (skill) {
        case CHARACTER_SKILL_MONSTER_ID: {
            if (CheckHiredNPCSpeciality(Hunter)) bonus = 6;
            if (CheckHiredNPCSpeciality(Sage)) bonus += 6;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_MONSTER_ID);
        } break;

        case CHARACTER_SKILL_ARMSMASTER: {
            if (CheckHiredNPCSpeciality(Armsmaster)) bonus = 2;
            if (CheckHiredNPCSpeciality(Weaponsmaster)) bonus += 3;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_ARMSMASTER);
        } break;

        case CHARACTER_SKILL_STEALING: {
            if (CheckHiredNPCSpeciality(Burglar)) bonus = 8;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_STEALING);
        } break;

        case CHARACTER_SKILL_ALCHEMY: {
            if (CheckHiredNPCSpeciality(Herbalist)) bonus = 4;
            if (CheckHiredNPCSpeciality(Apothecary)) bonus += 8;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_ALCHEMY);
        } break;

        case CHARACTER_SKILL_LEARNING: {
            if (CheckHiredNPCSpeciality(Teacher)) bonus = 10;
            if (CheckHiredNPCSpeciality(Instructor)) bonus += 15;
            if (CheckHiredNPCSpeciality(Scholar)) bonus += 5;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_LEARNING);
        } break;

        case CHARACTER_SKILL_UNARMED: {
            if (CheckHiredNPCSpeciality(Monk)) bonus = 2;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_UNARMED);
        } break;

        case CHARACTER_SKILL_DODGE: {
            if (CheckHiredNPCSpeciality(Monk)) bonus = 2;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_DODGE);
        } break;

        case CHARACTER_SKILL_BOW:
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_BOW);
            break;
        case CHARACTER_SKILL_SHIELD:
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_SHIELD);
            break;

        case CHARACTER_SKILL_EARTH:
            if (CheckHiredNPCSpeciality(Apprentice)) bonus = 2;
            if (CheckHiredNPCSpeciality(Mystic)) bonus += 3;
            if (CheckHiredNPCSpeciality(Spellmaster)) bonus += 4;
            if (classType == CHARACTER_CLASS_WARLOCK && PartyHasDragon())
                bonus += 3;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_EARTH);
            break;
        case CHARACTER_SKILL_FIRE:
            if (CheckHiredNPCSpeciality(Apprentice)) bonus = 2;
            if (CheckHiredNPCSpeciality(Mystic)) bonus += 3;
            if (CheckHiredNPCSpeciality(Spellmaster)) bonus += 4;
            if (classType == CHARACTER_CLASS_WARLOCK && PartyHasDragon())
                bonus += 3;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_FIRE);
            break;
        case CHARACTER_SKILL_AIR:
            if (CheckHiredNPCSpeciality(Apprentice)) bonus = 2;
            if (CheckHiredNPCSpeciality(Mystic)) bonus += 3;
            if (CheckHiredNPCSpeciality(Spellmaster)) bonus += 4;
            if (classType == CHARACTER_CLASS_WARLOCK && PartyHasDragon())
                bonus += 3;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_AIR);
            break;
        case CHARACTER_SKILL_WATER:
            if (CheckHiredNPCSpeciality(Apprentice)) bonus = 2;
            if (CheckHiredNPCSpeciality(Mystic)) bonus += 3;
            if (CheckHiredNPCSpeciality(Spellmaster)) bonus += 4;
            if (classType == CHARACTER_CLASS_WARLOCK && PartyHasDragon())
                bonus += 3;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_WATER);
            break;
        case CHARACTER_SKILL_SPIRIT:
            if (CheckHiredNPCSpeciality(Acolyte2)) bonus = 2;
            if (CheckHiredNPCSpeciality(Initiate)) bonus += 3;
            if (CheckHiredNPCSpeciality(Prelate)) bonus += 4;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_SPIRIT);
            break;
        case CHARACTER_SKILL_MIND:
            if (CheckHiredNPCSpeciality(Acolyte2)) bonus = 2;
            if (CheckHiredNPCSpeciality(Initiate)) bonus += 3;
            if (CheckHiredNPCSpeciality(Prelate)) bonus += 4;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_MIND);
            break;
        case CHARACTER_SKILL_BODY:
            if (CheckHiredNPCSpeciality(Acolyte2)) bonus = 2;
            if (CheckHiredNPCSpeciality(Initiate)) bonus += 3;
            if (CheckHiredNPCSpeciality(Prelate)) bonus += 4;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_BODY);
            break;
        case CHARACTER_SKILL_LIGHT:
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_LIGHT);
            break;
        case CHARACTER_SKILL_DARK: {
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_DARK);
        } break;

        case CHARACTER_SKILL_MERCHANT: {
            if (CheckHiredNPCSpeciality(Trader)) bonus = 4;
            if (CheckHiredNPCSpeciality(Merchant)) bonus += 6;
            if (CheckHiredNPCSpeciality(Gypsy)) bonus += 3;
            if (CheckHiredNPCSpeciality(Duper)) bonus += 8;
        } break;

        case CHARACTER_SKILL_PERCEPTION: {
            if (CheckHiredNPCSpeciality(Scout)) bonus = 6;
            if (CheckHiredNPCSpeciality(Psychic)) bonus += 5;
        } break;

        case CHARACTER_SKILL_ITEM_ID:
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_ITEM_ID);
            break;
        case CHARACTER_SKILL_MEDITATION:
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_MEDITATION);
            break;
        case CHARACTER_SKILL_TRAP_DISARM: {
            if (CheckHiredNPCSpeciality(Tinker)) bonus = 4;
            if (CheckHiredNPCSpeciality(Locksmith)) bonus += 6;
            if (CheckHiredNPCSpeciality(Burglar)) bonus += 8;
            bonus += GetItemsBonus(CHARACTER_ATTRIBUTE_SKILL_TRAP_DISARM);
        } break;
        default:
            break;
    }

    // Cap skill and bonus at 60.
    return std::min(skills_max_level[skill], bonus + base);
}

CombinedSkillValue Character::getActualSkillValue(CharacterSkillType skill) const {
    int level = actualSkillLevel(skill);
    CharacterSkillMastery mastery = pActiveSkills[skill].mastery();

    if (level > 0)
        mastery = std::max(mastery, CHARACTER_SKILL_MASTERY_NOVICE);

    return CombinedSkillValue(level, mastery);
}

//----- (0048FC00) --------------------------------------------------------
int Character::GetSkillBonus(CharacterAttributeType inSkill) const {
                    // TODO(_): move the individual implementations to attribute
                    // classes once possible ?? check
    int armsMasterBonus;

    armsMasterBonus = 0;
    int armmaster_skill = getActualSkillValue(CHARACTER_SKILL_ARMSMASTER).level();
    if (armmaster_skill > 0) {
        int multiplier = 0;
        if (inSkill == CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS) {
            multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_ARMSMASTER, 0, 0, 1, 2);
        } else if (inSkill == CHARACTER_ATTRIBUTE_ATTACK) {
            multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_ARMSMASTER, 0, 1, 1, 2);
        }
        armsMasterBonus = multiplier * armmaster_skill;
    }

    switch (inSkill) {
        case CHARACTER_ATTRIBUTE_RANGED_DMG_BONUS:
            if (HasItemEquipped(ITEM_SLOT_BOW)) {
                int bowSkillLevel = getActualSkillValue(CHARACTER_SKILL_BOW).level();
                int multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_BOW, 0, 0, 0, 1);
                return multiplier * bowSkillLevel;
            }
            return 0;
            break;
        case CHARACTER_ATTRIBUTE_HEALTH: {
            int base_value = pBaseHealthPerLevelByClass[classType];
            int attrib_modif = GetBodybuilding();
            return base_value * attrib_modif;
        } break;
        case CHARACTER_ATTRIBUTE_MANA: {
            int base_value = pBaseManaPerLevelByClass[classType];
            int attrib_modif = GetMeditation();
            return base_value * attrib_modif;
        } break;
        case CHARACTER_ATTRIBUTE_AC_BONUS: {
            bool wearingArmor = false;
            bool wearingLeather = false;
            unsigned int ACSum = 0;

            for (ITEM_SLOT j : allItemSlots()) {
                const ItemGen *currItem = GetNthEquippedIndexItem(j);
                if (currItem != nullptr && (!currItem->IsBroken())) {
                    CharacterSkillType itemSkillType =
                        (CharacterSkillType)currItem->GetPlayerSkillType();
                    int currArmorSkillLevel = 0;
                    int multiplier = 0;
                    switch (itemSkillType) {
                        case CHARACTER_SKILL_STAFF:
                            currArmorSkillLevel = getActualSkillValue(itemSkillType).level();
                            multiplier = GetMultiplierForSkillLevel(
                                itemSkillType, 0, 1, 1, 1);
                            break;
                        case CHARACTER_SKILL_SWORD:
                        case CHARACTER_SKILL_SPEAR:
                            currArmorSkillLevel = getActualSkillValue(itemSkillType).level();
                            multiplier = GetMultiplierForSkillLevel(
                                itemSkillType, 0, 0, 0, 1);
                            break;
                        case CHARACTER_SKILL_SHIELD:
                            currArmorSkillLevel = getActualSkillValue(itemSkillType).level();
                            wearingArmor = true;
                            multiplier = GetMultiplierForSkillLevel(
                                itemSkillType, 1, 1, 2, 2);
                            break;
                        case CHARACTER_SKILL_LEATHER:
                            currArmorSkillLevel = getActualSkillValue(itemSkillType).level();
                            wearingLeather = true;
                            multiplier = GetMultiplierForSkillLevel(
                                itemSkillType, 1, 1, 2, 2);
                            break;
                        case CHARACTER_SKILL_CHAIN:
                            currArmorSkillLevel = getActualSkillValue(itemSkillType).level();
                            wearingArmor = true;
                            multiplier = GetMultiplierForSkillLevel(
                                itemSkillType, 1, 1, 1, 1);
                            break;
                        case CHARACTER_SKILL_PLATE:
                            currArmorSkillLevel = getActualSkillValue(itemSkillType).level();
                            wearingArmor = true;
                            multiplier = GetMultiplierForSkillLevel(
                                itemSkillType, 1, 1, 1, 1);
                            break;
                        default:
                            break;
                    }
                    ACSum += multiplier * currArmorSkillLevel;
                }
            }

            CombinedSkillValue dodgeValue = getActualSkillValue(CHARACTER_SKILL_DODGE);
            int multiplier =
                GetMultiplierForSkillLevel(CHARACTER_SKILL_DODGE, 1, 2, 3, 3);
            if (!wearingArmor && (!wearingLeather || dodgeValue.mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER)) {
                ACSum += multiplier * dodgeValue.level();
            }
            return ACSum;
        } break;
        case CHARACTER_ATTRIBUTE_ATTACK:
            if (this->IsUnarmed()) {
                int unarmedSkill =
                    this->getActualSkillValue(CHARACTER_SKILL_UNARMED).level();
                if (!unarmedSkill) {
                    return 0;
                }
                int multiplier = GetMultiplierForSkillLevel(
                    CHARACTER_SKILL_UNARMED, 0, 1, 2, 2);
                return armsMasterBonus + multiplier * unarmedSkill;
            }
            for (ITEM_SLOT i : allItemSlots()) {  // ?? what eh check behaviour
                if (this->HasItemEquipped(i)) {
                    const ItemGen *currItem = GetNthEquippedIndexItem(i);
                    if (currItem->isMeleeWeapon()) {
                        CharacterSkillType currItemSkillType = currItem->GetPlayerSkillType();
                        int currentItemSkillLevel = this->getActualSkillValue(currItemSkillType).level();
                        if (currItemSkillType == CHARACTER_SKILL_BLASTER) {
                            int multiplier = GetMultiplierForSkillLevel(currItemSkillType, 1, 2, 3, 5);
                            return multiplier * currentItemSkillLevel;
                        } else if (currItemSkillType == CHARACTER_SKILL_STAFF && this->getActualSkillValue(CHARACTER_SKILL_UNARMED).level() > 0) {
                            int unarmedSkillLevel = this->getActualSkillValue(CHARACTER_SKILL_UNARMED).level();
                            int multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_UNARMED, 1, 1, 2, 2);
                            return multiplier * unarmedSkillLevel + armsMasterBonus + currentItemSkillLevel;
                        } else {
                            return armsMasterBonus + currentItemSkillLevel;
                        }
                    }
                }
            }
            return 0;
            break;

        case CHARACTER_ATTRIBUTE_RANGED_ATTACK:
            for (ITEM_SLOT i : allItemSlots()) {
                if (this->HasItemEquipped(i)) {
                    const ItemGen *currItemPtr = GetNthEquippedIndexItem(i);
                    if (currItemPtr->isWeapon()) {
                        CharacterSkillType currentItemSkillType = GetNthEquippedIndexItem(i)->GetPlayerSkillType();
                        int currentItemSkillLevel = this->getActualSkillValue(currentItemSkillType).level();
                        if (currentItemSkillType == CHARACTER_SKILL_BOW) {
                            int multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_BOW, 1, 1, 1, 1);
                            return multiplier * currentItemSkillLevel;
                        } else if (currentItemSkillType == CHARACTER_SKILL_BLASTER) {
                            int multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_BLASTER, 1, 2, 3, 5);
                            return multiplier * currentItemSkillLevel;
                        }
                    }
                }
            }
            return 0;
            break;

        case CHARACTER_ATTRIBUTE_MELEE_DMG_BONUS:
            if (this->IsUnarmed()) {
                int unarmedSkillLevel = this->getActualSkillValue(CHARACTER_SKILL_UNARMED).level();
                if (!unarmedSkillLevel) {
                    return 0;
                }
                int multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_UNARMED, 0, 1, 2, 2);
                return multiplier * unarmedSkillLevel;
            }
            for (ITEM_SLOT i : allItemSlots()) {
                if (this->HasItemEquipped(i)) {
                    const ItemGen *currItemPtr = GetNthEquippedIndexItem(i);
                    if (currItemPtr->isMeleeWeapon()) {
                        CharacterSkillType currItemSkillType = currItemPtr->GetPlayerSkillType();
                        int currItemSkillLevel = this->getActualSkillValue(currItemSkillType).level();
                        int baseSkillBonus;
                        int multiplier;
                        switch (currItemSkillType) {
                            case CHARACTER_SKILL_STAFF:

                                if (this->getActualSkillValue(CHARACTER_SKILL_STAFF).mastery() >= CHARACTER_SKILL_MASTERY_GRANDMASTER &&
                                    this->getActualSkillValue(CHARACTER_SKILL_UNARMED).level() > 0) {
                                    int unarmedSkillLevel = this->getActualSkillValue(CHARACTER_SKILL_UNARMED).level();
                                    int multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_UNARMED, 0, 1, 2, 2);
                                    return multiplier * unarmedSkillLevel;
                                } else {
                                    return armsMasterBonus;
                                }
                                break;

                            case CHARACTER_SKILL_DAGGER:
                                multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_DAGGER, 0, 0, 0, 1);
                                baseSkillBonus = multiplier * currItemSkillLevel;
                                return armsMasterBonus + baseSkillBonus;
                            case CHARACTER_SKILL_SWORD:
                                multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_SWORD, 0, 0, 0, 0);
                                baseSkillBonus = multiplier * currItemSkillLevel;
                                return armsMasterBonus + baseSkillBonus;
                            case CHARACTER_SKILL_MACE:
                            case CHARACTER_SKILL_SPEAR:
                                multiplier = GetMultiplierForSkillLevel(currItemSkillType, 0, 1, 1, 1);
                                baseSkillBonus = multiplier * currItemSkillLevel;
                                return armsMasterBonus + baseSkillBonus;
                            case CHARACTER_SKILL_AXE:
                                multiplier = GetMultiplierForSkillLevel(CHARACTER_SKILL_AXE, 0, 0, 1, 1);
                                baseSkillBonus = multiplier * currItemSkillLevel;
                                return armsMasterBonus + baseSkillBonus;
                            default:
                                break;
                        }
                    }
                }
            }
            return 0;
            break;
        default:
            return 0;
    }
}

unsigned int Character::GetMultiplierForSkillLevel(
    CharacterSkillType uSkillType, int mult1, int mult2, int mult3,
    int mult4) const {  // TODO(pskelton): ?? needs changing - check behavious
    CharacterSkillMastery masteryLvl = getActualSkillValue(uSkillType).mastery();
    switch (masteryLvl) {
        case CHARACTER_SKILL_MASTERY_NONE:
            return 0;
        case CHARACTER_SKILL_MASTERY_NOVICE:
            return mult1;
        case CHARACTER_SKILL_MASTERY_EXPERT:
            return mult2;
        case CHARACTER_SKILL_MASTERY_MASTER:
            return mult3;
        case CHARACTER_SKILL_MASTERY_GRANDMASTER:
            return mult4;
    }
    Error("(%u)", masteryLvl);
    return 0;
}

//----- (00490109) --------------------------------------------------------
// faces are:  0  1  2  3   human males
//             4  5  6  7   human females
//                   8  9   elf males
//                  10 11   elf females
//                  12 13   dwarf males
//                  14 15   dwarf females
//                  16 17   goblin males
//                  18 19   goblin females
//                     20   lich male
//                     21   lich female
//                     22   underwater suits (unused)
//                     23   zombie male
//                     24   zombie female
enum CharacterRace Character::GetRace() const {
    if (uCurrentFace <= 7) {
        return CHARACTER_RACE_HUMAN;
    } else if (uCurrentFace <= 11) {
        return CHARACTER_RACE_ELF;
    } else if (uCurrentFace <= 15) {
        return CHARACTER_RACE_DWARF;
    } else if (uCurrentFace <= 19) {
        return CHARACTER_RACE_GOBLIN;
    } else {
        return CHARACTER_RACE_HUMAN;
    }
}

std::string Character::GetRaceName() const {
    switch (GetRace()) {
        case CHARACTER_RACE_HUMAN: return localization->GetString(LSTR_RACE_HUMAN);
        case CHARACTER_RACE_ELF: return localization->GetString(LSTR_RACE_ELF);
        case CHARACTER_RACE_GOBLIN: return localization->GetString(LSTR_RACE_GOBLIN);
        case CHARACTER_RACE_DWARF: return localization->GetString(LSTR_RACE_DWARF);
        default:
            __debugbreak();
            return std::string();  // Make the compiler happy.
    }
}

//----- (00490141) --------------------------------------------------------
CharacterSex Character::GetSexByVoice() const {
    switch (this->uVoiceID) {
        case 0u:
        case 1u:
        case 2u:
        case 3u:
        case 8u:
        case 9u:
        case 0xCu:
        case 0xDu:
        case 0x10u:
        case 0x11u:
        case 0x14u:
        case 0x17u:
            return SEX_MALE;

        case 4u:
        case 5u:
        case 6u:
        case 7u:
        case 0xAu:
        case 0xBu:
        case 0xEu:
        case 0xFu:
        case 0x12u:
        case 0x13u:
        case 0x15u:
        case 0x18u:
            return SEX_FEMALE;
    }
    Error("(%u)", this->uVoiceID);
    return SEX_MALE;
}

//----- (00490188) --------------------------------------------------------
void Character::SetInitialStats() {
    CharacterRace race = GetRace();
    uMight = StatTable[race][0].uBaseValue;
    uIntelligence = StatTable[race][1].uBaseValue;
    uPersonality = StatTable[race][2].uBaseValue;
    uEndurance = StatTable[race][3].uBaseValue;
    uAccuracy = StatTable[race][4].uBaseValue;
    uSpeed = StatTable[race][5].uBaseValue;
    uLuck = StatTable[race][6].uBaseValue;
}

//----- (004901FC) --------------------------------------------------------
void Character::SetSexByVoice() {
    switch (this->uVoiceID) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 8:
        case 9:
        case 0xC:
        case 0xD:
        case 0x10:
        case 0x11:
        case 0x14:
        case 0x17:
            this->uSex = SEX_MALE;
            break;
        case 4:
        case 5:
        case 6:
        case 7:
        case 0xA:
        case 0xB:
        case 0xE:
        case 0xF:
        case 0x12:
        case 0x13:
        case 0x15:
        case 0x18:
            this->uSex = SEX_FEMALE;
            break;
        default:
            Error("(%u)", this->uVoiceID);
            break;
    }
}

//----- (0049024A) --------------------------------------------------------
void Character::Reset(CharacterClassType cls) {
    sLevelModifier = 0;
    sAgeModifier = 0;

    classType = cls;
    uLuckBonus = 0;
    uSpeedBonus = 0;
    uAccuracyBonus = 0;
    uEnduranceBonus = 0;
    uPersonalityBonus = 0;
    uIntelligenceBonus = 0;
    uMightBonus = 0;
    uLevel = 1;
    experience = 251ll + grng->random(100);
    uSkillPoints = 0;
    uBirthYear = 1147 - grng->random(6);
    pActiveSkills.fill(CombinedSkillValue());
    pActiveSkills[CHARACTER_SKILL_CLUB] = CombinedSkillValue::novice(); // Hidden skills, always known.
    pActiveSkills[CHARACTER_SKILL_MISC] = CombinedSkillValue::novice();
    _achievedAwardsBits.reset();
    memset(&spellbook, 0, sizeof(spellbook));
    uQuickSpell = SPELL_NONE;

    for (CharacterSkillType i : allSkills()) {
        if (pSkillAvailabilityPerClass[classType / 4][i] != CLASS_SKILL_PRIMARY)
            continue;

        setSkillValue(i, CombinedSkillValue::novice());
    }

    memset(&pEquipment, 0, sizeof(CharacterEquipment));
    pInventoryMatrix.fill(0);
    for (uint i = 0; i < INVENTORY_SLOT_COUNT; ++i) pInventoryItemList[i].Reset();
    for (uint i = 0; i < ADDITIONAL_SLOT_COUNT; ++i) pEquippedItems[i].Reset();

    health = GetMaxHealth();
    mana = GetMaxMana();
}

//----- (004903C9) --------------------------------------------------------
CharacterSkillType Character::GetSkillIdxByOrder(signed int order) {
    int counter;  // edx@5
    bool canBeInactive;
    ClassSkillAffinity requiredValue;
    signed int offset;

    if (order <= 1) {
        canBeInactive = false;
        requiredValue = CLASS_SKILL_PRIMARY;  // 2 - primary skill
        offset = 0;
    } else if (order <= 3) {
        canBeInactive = false;
        requiredValue = CLASS_SKILL_AVAILABLE;  // 1 - available
        offset = 2;
    } else if (order <= 12) {
        canBeInactive = true;
        requiredValue = CLASS_SKILL_AVAILABLE;  // 1 - available
        offset = 4;
    } else {
        return CHARACTER_SKILL_INVALID;
    }
    counter = 0;
    for (CharacterSkillType i : allVisibleSkills()) {
        if ((this->pActiveSkills[i] || canBeInactive) &&
            pSkillAvailabilityPerClass[classType / 4][i] == requiredValue) {
            if (counter == order - offset) return i;
            ++counter;
        }
    }

    return CHARACTER_SKILL_INVALID;
}

//----- (0049048D) --------------------------------------------------------
// uint16_t PartyCreation_BtnMinusClick(Character *_this, int eAttribute)
void Character::DecreaseAttribute(int eAttribute) {
    int pBaseValue;    // ecx@1
    int pDroppedStep;  // ebx@1
    int pStep;         // esi@1
    int uMinValue;     // [sp+Ch] [bp-4h]@1

    int raceId = GetRace();
    pBaseValue = StatTable[raceId][eAttribute].uBaseValue;
    pDroppedStep = StatTable[raceId][eAttribute].uDroppedStep;
    uMinValue = pBaseValue - 2;
    pStep = StatTable[raceId][eAttribute].uBaseStep;
    unsigned short *AttrToChange = nullptr;
    switch (eAttribute) {
        case CHARACTER_ATTRIBUTE_MIGHT:
            AttrToChange = &this->uMight;
            break;
        case CHARACTER_ATTRIBUTE_INTELLIGENCE:
            AttrToChange = &this->uIntelligence;
            break;
        case CHARACTER_ATTRIBUTE_PERSONALITY:
            AttrToChange = &this->uPersonality;
            break;
        case CHARACTER_ATTRIBUTE_ENDURANCE:
            AttrToChange = &this->uEndurance;
            break;
        case CHARACTER_ATTRIBUTE_ACCURACY:
            AttrToChange = &this->uAccuracy;
            break;
        case CHARACTER_ATTRIBUTE_SPEED:
            AttrToChange = &this->uSpeed;
            break;
        case CHARACTER_ATTRIBUTE_LUCK:
            AttrToChange = &this->uLuck;
            break;
    }
    if (*AttrToChange <= pBaseValue) pStep = pDroppedStep;
    if (*AttrToChange - pStep >= uMinValue) *AttrToChange -= pStep;
}

//----- (004905F5) --------------------------------------------------------
// signed int  PartyCreation_BtnPlusClick(Character *this, int eAttribute)
void Character::IncreaseAttribute(int eAttribute) {
    int raceId;              // eax@1
    int maxValue;            // ebx@1
    signed int baseStep;     // edi@1
    signed int tmp;          // eax@17
    signed int result;       // eax@18
    int baseValue;           // [sp+Ch] [bp-8h]@1
    signed int droppedStep;  // [sp+10h] [bp-4h]@1
    unsigned short *statToChange;

    raceId = GetRace();
    maxValue = StatTable[raceId][eAttribute].uMaxValue;
    baseStep = StatTable[raceId][eAttribute].uBaseStep;
    baseValue = StatTable[raceId][eAttribute].uBaseValue;
    droppedStep = StatTable[raceId][eAttribute].uDroppedStep;
    CharacterCreation_GetUnspentAttributePointCount();
    switch (eAttribute) {
        case 0:
            statToChange = &this->uMight;
            break;
        case 1:
            statToChange = &this->uIntelligence;
            break;
        case 2:
            statToChange = &this->uPersonality;
            break;
        case 3:
            statToChange = &this->uEndurance;
            break;
        case 4:
            statToChange = &this->uAccuracy;
            break;
        case 5:
            statToChange = &this->uSpeed;
            break;
        case 6:
            statToChange = &this->uLuck;
            break;
        default:
            Error("(%u)", eAttribute);
            break;
    }
    if (*statToChange < baseValue) {
        tmp = baseStep;
        baseStep = droppedStep;
        droppedStep = tmp;
    }
    result = CharacterCreation_GetUnspentAttributePointCount();
    if (result >= droppedStep) {
        if (baseStep + *statToChange <= maxValue) *statToChange += baseStep;
    }
}

//----- (0049070F) --------------------------------------------------------
void Character::resetTempBonuses() {
    // this is also used during party rest and heal so only buffs and bonuses are reset
    this->sLevelModifier = 0;
    this->sACModifier = 0;
    this->uLuckBonus = 0;
    this->uAccuracyBonus = 0;
    this->uSpeedBonus = 0;
    this->uEnduranceBonus = 0;
    this->uPersonalityBonus = 0;
    this->uIntelligenceBonus = 0;
    this->uMightBonus = 0;
    this->field_100 = 0;
    this->field_FC = 0;
    this->field_F8 = 0;
    this->field_F4 = 0;
    this->field_F0 = 0;
    this->field_EC = 0;
    this->field_E8 = 0;
    this->field_E4 = 0;
    this->field_E0 = 0;
    this->sResFireBonus = 0;
    this->sResAirBonus = 0;
    this->sResWaterBonus = 0;
    this->sResEarthBonus = 0;
    this->sResPhysicalBonus = 0;
    this->sResMagicBonus = 0;
    this->sResSpiritBonus = 0;
    this->sResMindBonus = 0;
    this->sResBodyBonus = 0;
    this->sResLightBonus = 0;
    this->sResDarkBonus = 0;

    this->_ranged_dmg_bonus = 0;
    this->_ranged_atk_bonus = 0;
    this->_melee_dmg_bonus = 0;
    this->_some_attack_bonus = 0;
    this->_mana_related = 0;
    this->uFullManaBonus = 0;
    this->_health_related = 0;
    this->uFullHealthBonus = 0;
}

//----- (004907E7) --------------------------------------------------------
Color Character::GetStatColor(int uStat) const {
    int attribute_value;  // edx@1

    int base_attribute_value = StatTable[GetRace()][uStat].uBaseValue;
    switch (uStat) {
        case 0:
            attribute_value = uMight;
            break;
        case 1:
            attribute_value = uIntelligence;
            break;
        case 2:
            attribute_value = uPersonality;
            break;
        case 3:
            attribute_value = uEndurance;
            break;
        case 4:
            attribute_value = uAccuracy;
            break;
        case 5:
            attribute_value = uSpeed;
            break;
        case 6:
            attribute_value = uLuck;
            break;
        default:
            Error("Unexpected attribute");
    }

    if (attribute_value == base_attribute_value)
        return ui_character_stat_default_color;
    else if (attribute_value > base_attribute_value)
        return ui_character_stat_buffed_color;
    else
        return ui_character_stat_debuffed_color;
}

//----- (004908A8) --------------------------------------------------------
bool Character::DiscardConditionIfLastsLongerThan(Condition uCondition,
                                               GameTime time) {
    if (conditions.Has(uCondition) && time < conditions.Get(uCondition)) {
        conditions.Reset(uCondition);
        return true;
    } else {
        return false;
    }
}

void Character::useItem(int targetCharacter, bool isPortraitClick) {
    Character *playerAffected = &pParty->pCharacters[targetCharacter];
    if (pParty->bTurnBasedModeOn && (pTurnEngine->turn_stage == TE_WAIT || pTurnEngine->turn_stage == TE_MOVEMENT)) {
        return;
    }
    if (pParty->pPickedItem.isReagent()) {
        // TODO(Nik-RE-dev): this looks like some artifact from MM6 (where you can eat reagents)
        // In MM7 these item IDs are invalid (plus index 161 used twice which is wrong)
        if (pParty->pPickedItem.uItemID == ITEM_161) {
            playerAffected->SetCondition(CONDITION_POISON_WEAK, 1);
        } else if (pParty->pPickedItem.uItemID == ITEM_161) {
            playerAffected->mana += 2;
            if (playerAffected->mana > playerAffected->GetMaxMana()) {
                playerAffected->mana = playerAffected->GetMaxMana();
            }
            playerAffected->playReaction(SPEECH_DRINK_POTION);
        } else if (pParty->pPickedItem.uItemID == ITEM_162) {
            playerAffected->Heal(2);
            playerAffected->playReaction(SPEECH_DRINK_POTION);
        } else {
            GameUI_SetStatusBar(LSTR_FMT_S_CANT_BE_USED_THIS_WAY, pParty->pPickedItem.GetDisplayName());
            pAudioPlayer->playUISound(SOUND_error);
            return;
        }
        pAudioPlayer->playUISound(SOUND_eat);

        if (pGUIWindow_CurrentMenu &&
            pGUIWindow_CurrentMenu->eWindowType != WINDOW_null) {
            engine->_messageQueue->addMessageCurrentFrame(UIMSG_Escape, 0, 0);
        }
        //if (v73) {
            if (pParty->bTurnBasedModeOn) {
                pParty->pTurnBasedCharacterRecoveryTimes[targetCharacter] = 100;
                this->SetRecoveryTime(100);
                pTurnEngine->ApplyPlayerAction();
            } else {
                this->SetRecoveryTime((int)(debug_non_combat_recovery_mul * flt_debugrecmod3 * 100.0));
            }
        //}
        mouse->RemoveHoldingItem();
        return;
    }

    if (pParty->pPickedItem.isPotion()) {
        // TODO(Nik-RE-dev): no CanAct check?
        int potionStrength = pParty->pPickedItem.uEnchantmentType;
        GameTime buffDuration = GameTime::FromMinutes(30 * potionStrength); // all buffs have same duration based on potion strength
        switch (pParty->pPickedItem.uItemID) {
            case ITEM_POTION_CATALYST:
                playerAffected->SetCondition(CONDITION_POISON_WEAK, 1);
                break;

            case ITEM_POTION_CURE_WOUNDS:
                playerAffected->Heal(potionStrength + 10);
                break;

            case ITEM_POTION_MAGIC:
                playerAffected->mana += potionStrength + 10;
                if (playerAffected->mana > playerAffected->GetMaxMana()) {
                    playerAffected->mana = playerAffected->GetMaxMana();
                }
                break;

            case ITEM_POTION_CURE_WEAKNESS:
                playerAffected->conditions.Reset(CONDITION_WEAK);
                break;

            case ITEM_POTION_CURE_DISEASE:
                playerAffected->conditions.Reset(CONDITION_DISEASE_SEVERE);
                playerAffected->conditions.Reset(CONDITION_DISEASE_MEDIUM);
                playerAffected->conditions.Reset(CONDITION_DISEASE_WEAK);
                break;

            case ITEM_POTION_CURE_POISON:
                playerAffected->conditions.Reset(CONDITION_POISON_SEVERE);
                playerAffected->conditions.Reset(CONDITION_POISON_MEDIUM);
                playerAffected->conditions.Reset(CONDITION_POISON_WEAK);
                break;

            case ITEM_POTION_AWAKEN:
                playerAffected->conditions.Reset(CONDITION_SLEEP);
                break;

            case ITEM_POTION_HASTE:
                if (!playerAffected->conditions.Has(CONDITION_WEAK)) {
                    playerAffected->pCharacterBuffs[CHARACTER_BUFF_HASTE].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER, 5, 0, 0);
                }
                break;

            case ITEM_POTION_HEROISM:
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_HEROISM].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER, 5, 0, 0);
                break;

            case ITEM_POTION_BLESS:
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_BLESS].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER, 5, 0, 0);
                break;

            case ITEM_POTION_PRESERVATION:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_PRESERVATION].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_SHIELD:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_SHIELD].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_STONESKIN:
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_STONESKIN].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER, 5, 0, 0);
                break;

            case ITEM_POTION_WATER_BREATHING:
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_WATER_WALK].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER, 5, 0, 0);
                // Drink potion reaction was missing
                break;

            case ITEM_POTION_REMOVE_FEAR:
                playerAffected->conditions.Reset(CONDITION_FEAR);
                break;

            case ITEM_POTION_REMOVE_CURSE:
                playerAffected->conditions.Reset(CONDITION_CURSED);
                break;

            case ITEM_POTION_CURE_INSANITY:
                playerAffected->conditions.Reset(CONDITION_INSANE);
                break;

            case ITEM_POTION_MIGHT_BOOST:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_STRENGTH].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_INTELLECT_BOOST:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_INTELLIGENCE].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_PERSONALITY_BOOST:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_PERSONALITY].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_ENDURANCE_BOOST:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_ENDURANCE].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_SPEED_BOOST:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_SPEED].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_ACCURACY_BOOST:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_ACCURACY].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_CURE_PARALYSIS:
                playerAffected->conditions.Reset(CONDITION_PARALYZED);
                break;

            case ITEM_POTION_DIVINE_RESTORATION:
            {
                GameTime deadTime = playerAffected->conditions.Get(CONDITION_DEAD);
                GameTime petrifedTime = playerAffected->conditions.Get(CONDITION_PETRIFIED);
                GameTime eradicatedTime = playerAffected->conditions.Get(CONDITION_ERADICATED);
                // TODO(Nik-RE-dev): why not playerAffected?
                conditions.ResetAll();
                playerAffected->conditions.Set(CONDITION_DEAD, deadTime);
                playerAffected->conditions.Set(CONDITION_PETRIFIED, petrifedTime);
                playerAffected->conditions.Set(CONDITION_ERADICATED, eradicatedTime);
                break;
            }

            case ITEM_POTION_DIVINE_CURE:
                playerAffected->Heal(5 * potionStrength);
                break;

            case ITEM_POTION_DIVINE_POWER:
                playerAffected->mana += 5 * potionStrength;
                if (playerAffected->mana > playerAffected->GetMaxMana())
                    playerAffected->mana = playerAffected->GetMaxMana();
                break;

            case ITEM_POTION_LUCK_BOOST:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_LUCK].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_FIRE_RESISTANCE:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_RESIST_FIRE].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_AIR_RESISTANCE:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_RESIST_AIR].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_WATER_RESISTANCE:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_RESIST_WATER].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_EARTH_RESISTANCE:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_RESIST_EARTH].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_MIND_RESISTANCE:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_RESIST_MIND].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_BODY_RESISTANCE:
                // mastery was NONE
                playerAffected->pCharacterBuffs[CHARACTER_BUFF_RESIST_BODY].Apply(pParty->GetPlayingTime() + buffDuration, CHARACTER_SKILL_MASTERY_MASTER,
                        potionStrength * 3, 0, 0);
                break;

            case ITEM_POTION_STONE_TO_FLESH:
                playerAffected->conditions.Reset(CONDITION_PETRIFIED);
                break;

            case ITEM_POTION_PURE_LUCK:
                if (!playerAffected->pure_luck_used) {
                    playerAffected->uLuck += 50;
                    playerAffected->pure_luck_used = 1;
                }
                break;

            case ITEM_POTION_PURE_SPEED:
                if (!playerAffected->pure_speed_used) {
                    playerAffected->uSpeed += 50;
                    playerAffected->pure_speed_used = 1;
                }
                break;

            case ITEM_POTION_PURE_INTELLECT:
                if (!playerAffected->pure_intellect_used) {
                    playerAffected->uIntelligence += 50;
                    playerAffected->pure_intellect_used = 1;
                }
                break;

            case ITEM_POTION_PURE_ENDURANCE:
                if (!playerAffected->pure_endurance_used) {
                    playerAffected->uEndurance += 50;
                    playerAffected->pure_endurance_used = 1;
                }
                break;

            case ITEM_POTION_PURE_PERSONALITY:
                if (!playerAffected->pure_personality_used) {
                    playerAffected->uPersonality += 50;
                    playerAffected->pure_personality_used = 1;
                }
                break;

            case ITEM_POTION_PURE_ACCURACY:
                if (!playerAffected->pure_accuracy_used) {
                    playerAffected->uAccuracy += 50;
                    playerAffected->pure_accuracy_used = 1;
                }
                break;

            case ITEM_POTION_PURE_MIGHT:
                if (!playerAffected->pure_might_used) {
                    playerAffected->uMight += 50;
                    playerAffected->pure_might_used = 1;
                }
                break;

            case ITEM_POTION_REJUVENATION:
                playerAffected->sAgeModifier = 0;
                break;

            default:
                GameUI_SetStatusBar(LSTR_FMT_S_CANT_BE_USED_THIS_WAY, pParty->pPickedItem.GetDisplayName());
                pAudioPlayer->playUISound(SOUND_error);
                return;
        }
        if (pParty->pPickedItem.uItemID != ITEM_POTION_CATALYST) {
            playerAffected->playReaction(SPEECH_DRINK_POTION);
        }
        pAudioPlayer->playUISound(SOUND_drink);
        if (pGUIWindow_CurrentMenu && pGUIWindow_CurrentMenu->eWindowType != WINDOW_null) {
            engine->_messageQueue->addMessageCurrentFrame(UIMSG_Escape, 0, 0);
        }
        if (pParty->bTurnBasedModeOn) {
            pParty->pTurnBasedCharacterRecoveryTimes[targetCharacter] = 100;
            this->SetRecoveryTime(100);
            pTurnEngine->ApplyPlayerAction();
        } else {
            this->SetRecoveryTime(debug_non_combat_recovery_mul * flt_debugrecmod3 * 100.0);
        }
        mouse->RemoveHoldingItem();
        return;
    }

    if (pParty->pPickedItem.isSpellScroll()) {
        if (current_screen_type == SCREEN_CASTING) {
            return;
        }
        if (!playerAffected->CanAct()) {
            GameUI_SetStatusBar(LSTR_FMT_THAT_PLAYER_IS_S, localization->GetCharacterConditionName(playerAffected->GetMajorConditionIdx()));
            pAudioPlayer->playUISound(SOUND_error);
            return;
        }
        if (engine->IsUnderwater()) {
            GameUI_SetStatusBar(LSTR_CANT_DO_UNDERWATER);
            pAudioPlayer->playUISound(SOUND_error);
            return;
        }

        // TODO(Nik-RE-dev): spell scroll is removed before actual casting and will be consumed even if casting is canceled.
        SPELL_TYPE scrollSpellId = scrollSpellIds[pParty->pPickedItem.uItemID];
        if (isSpellTargetsItem(scrollSpellId)) {
            mouse->RemoveHoldingItem();
            pGUIWindow_CurrentMenu->Release();
            current_screen_type = SCREEN_GAME;
            pushScrollSpell(scrollSpellId, targetCharacter);
        } else {
            mouse->RemoveHoldingItem();
            // Process spell on next frame after game exits inventory window.
            engine->_messageQueue->addMessageNextFrame(UIMSG_SpellScrollUse, scrollSpellId, targetCharacter);
            if (current_screen_type != SCREEN_GAME && pGUIWindow_CurrentMenu && (pGUIWindow_CurrentMenu->eWindowType != WINDOW_null)) {
                engine->_messageQueue->addMessageCurrentFrame(UIMSG_Escape, 0, 0);
            }
        }
        return;
    }

    if (pParty->pPickedItem.isBook()) {
        SPELL_TYPE bookSpellId = bookSpellIds[pParty->pPickedItem.uItemID];
        if (playerAffected->spellbook.bHaveSpell[bookSpellId - SPELL_FIRST_REGULAR]) {
            GameUI_SetStatusBar(LSTR_FMT_YOU_ALREADY_KNOW_S_SPELL, pParty->pPickedItem.GetDisplayName());
            pAudioPlayer->playUISound(SOUND_error);
            return;
        }
        if (!playerAffected->CanAct()) {
            GameUI_SetStatusBar(LSTR_FMT_THAT_PLAYER_IS_S, localization->GetCharacterConditionName(playerAffected->GetMajorConditionIdx()));
            pAudioPlayer->playUISound(SOUND_error);
            return;
        }

        CharacterSkillMastery requiredMastery = pSpellDatas[bookSpellId].skillMastery;
        CharacterSkillType skill = getSkillTypeForSpell(bookSpellId);
        CombinedSkillValue val = playerAffected->getSkillValue(skill);

        if (requiredMastery > val.mastery() || val.level() == 0) {
            GameUI_SetStatusBar(LSTR_FMT_DONT_HAVE_SKILL_TO_LEAN_S, pParty->pPickedItem.GetDisplayName());
            playerAffected->playReaction(SPEECH_CANT_LEARN_SPELL);
            return;
        }
        playerAffected->spellbook.bHaveSpell[bookSpellId - SPELL_FIRST_REGULAR] = 1;
        playerAffected->playReaction(SPEECH_LEARN_SPELL);

        // if (pGUIWindow_CurrentMenu && pGUIWindow_CurrentMenu->eWindowType != WINDOW_null) {
        //     if (!v73) { // v73 is always 0 at this point
        //         mouse->RemoveHoldingItem();
        //         return;
        //     }
        //     engine->_messageQueue->addMessageCurrentFrame(UIMSG_Escape, 0, 0);
        // }
        //       if ( v73 ) // v73 is always 0 at this point
        //       {
        //         if ( pParty->bTurnBasedModeOn )
        //         {
        //           pParty->pTurnBasedCharacterRecoveryTimes[player_num-1] = 100;
        //           thisb->SetRecoveryTime(100);
        //           pTurnEngine->ApplyPlayerAction();
        //         }
        //         else
        //         {
        //           thisb->SetRecoveryTime(flt_6BE3A4_debug_recmod1 * 213.3333333333333);
        //         }
        //       }
        mouse->RemoveHoldingItem();
        return;
    }

    if (pParty->pPickedItem.isMessageScroll()) {
        if (playerAffected->CanAct()) {
            CreateMsgScrollWindow(pParty->pPickedItem.uItemID);
            playerAffected->playReaction(SPEECH_READ_SCROLL);
            return;
        }

        GameUI_SetStatusBar(LSTR_FMT_THAT_PLAYER_IS_S, localization->GetCharacterConditionName(playerAffected->GetMajorConditionIdx()));
        pAudioPlayer->playUISound(SOUND_error);
        return;
    }

    // Everything else
    {
        if (pParty->pPickedItem.uItemID == ITEM_GENIE_LAMP) {
            int value = pParty->uCurrentMonthWeek + 1;

            std::string status;
            switch (pParty->uCurrentMonth) {
                case 0: // Jan
                    playerAffected->uMight += value;
                    status = fmt::format("+{} {} {}", value, localization->GetAttirubteName(0), localization->GetString(LSTR_PERMANENT));
                    break;
                case 1: // Feb
                    playerAffected->uIntelligence += value;
                    status = fmt::format("+{} {} {}", value, localization->GetAttirubteName(1), localization->GetString(LSTR_PERMANENT));
                    break;
                case 2: // Mar
                    playerAffected->uPersonality += value;
                    status = fmt::format("+{} {} {}", value, localization->GetAttirubteName(2), localization->GetString(LSTR_PERMANENT));
                    break;
                case 3: // Apr
                    playerAffected->uEndurance += value;
                    status = fmt::format("+{} {} {}", value, localization->GetAttirubteName(3), localization->GetString(LSTR_PERMANENT));
                    break;
                case 4: // May
                    playerAffected->uAccuracy += value;
                    status = fmt::format("+{} {} {}", value, localization->GetAttirubteName(4), localization->GetString(LSTR_PERMANENT));
                    break;
                case 5: // Jun
                    playerAffected->uSpeed += value;
                    status = fmt::format("+{} {} {}", value, localization->GetAttirubteName(5), localization->GetString(LSTR_PERMANENT));
                    break;
                case 6: // Jul
                    playerAffected->uLuck += value;
                    status = fmt::format("+{} {} {}", value, localization->GetAttirubteName(6), localization->GetString(LSTR_PERMANENT));
                    break;
                case 7: // Aug
                    pParty->partyFindsGold(1000 * value, GOLD_RECEIVE_SHARE);
                    status = fmt::format("+{} {}", 1000 * value, localization->GetString(LSTR_GOLD));
                    break;
                case 8: // Sep
                    pParty->GiveFood(5 * value);
                    status = fmt::format("+{} {}", 5 * value, localization->GetString(LSTR_FOOD));
                    break;
                case 9: // Oct
                    playerAffected->uSkillPoints += 2 * value;
                    status = fmt::format("+{} {}", 2 * value, localization->GetString(LSTR_SKILL_POINTS));
                    break;
                case 10: // Nov
                    playerAffected->experience += 2500ll * value;
                    status = fmt::format("+{} {}", 2500ll * value, localization->GetString(LSTR_EXPERIENCE));
                    break;
                case 11: { // Dec
                    int res = grng->random(6);

                    // No spirit res
                    res = (res == 5 ? res + 1 : res);

                    const char *spell_school_name = localization->GetSpellSchoolName(res);

                    switch (res) {
                        case 0:  // Fire
                            playerAffected->sResFireBase += value;
                            break;
                        case 1:  // Air
                            playerAffected->sResAirBase += value;
                            break;
                        case 2:  // Water
                            playerAffected->sResWaterBase += value;
                            break;
                        case 3:  // Earth
                            playerAffected->sResEarthBase += value;
                            break;
                        case 4:  // Mind
                            playerAffected->sResMindBase += value;
                            break;
                        case 6:  // Body
                            playerAffected->sResBodyBase += value;
                            break;
                        default:
                            // ("Unexpected attribute");
                            return;
                    }
                    status = fmt::format("+{} {} {}", value, spell_school_name, localization->GetString(LSTR_PERMANENT));
                    break;
                }
            }
            GameUI_SetStatusBar(status);

            spell_fx_renderer->SetPlayerBuffAnim(SPELL_QUEST_COMPLETED, targetCharacter);
            playerAffected->playReaction(SPEECH_QUEST_GOT);
            pAudioPlayer->playUISound(SOUND_chimes);
            if (pParty->uCurrentDayOfMonth == 6 || pParty->uCurrentDayOfMonth == 20) {
                playerAffected->SetCondition(CONDITION_ERADICATED, 0);
                pAudioPlayer->playUISound(SOUND_gong);
            } else if (pParty->uCurrentDayOfMonth == 12 || pParty->uCurrentDayOfMonth == 26) {
                playerAffected->SetCondition(CONDITION_DEAD, 0);
                pAudioPlayer->playUISound(SOUND_gong);
            } else if (pParty->uCurrentDayOfMonth == 4 || pParty->uCurrentDayOfMonth == 25) {
                playerAffected->SetCondition(CONDITION_PETRIFIED, 0);
                pAudioPlayer->playUISound(SOUND_gong);
            }
        } else if (pParty->pPickedItem.uItemID == ITEM_RED_APPLE) {
            pParty->GiveFood(1);
            pAudioPlayer->playUISound(SOUND_eat);
        } else if (pParty->pPickedItem.uItemID == ITEM_QUEST_LUTE) {
            pAudioPlayer->playUISound(SOUND_luteguitar);
            return;
        } else if (pParty->pPickedItem.uItemID == ITEM_QUEST_FAERIE_PIPES) {
            pAudioPlayer->playUISound(SOUND_panflute);
            return;
        } else if (pParty->pPickedItem.uItemID == ITEM_QUEST_GRYPHONHEARTS_TRUMPET) {
            pAudioPlayer->playUISound(SOUND_trumpet);
            return;
        } else if (pParty->pPickedItem.uItemID == ITEM_HORSESHOE) {
            spell_fx_renderer->SetPlayerBuffAnim(SPELL_QUEST_COMPLETED, targetCharacter);
            //v5 = PID(OBJECT_Character, player_num + 49);
            //pAudioPlayer->playSound(SOUND_quest, v5);
            pAudioPlayer->playUISound(SOUND_quest);
            playerAffected->uSkillPoints += 2;
        } else if (pParty->pPickedItem.uItemID == ITEM_TEMPLE_IN_A_BOTTLE) {
            TeleportToNWCDungeon();
            return;
        } else {
            GameUI_SetStatusBar(LSTR_FMT_S_CANT_BE_USED_THIS_WAY, pParty->pPickedItem.GetDisplayName());
            pAudioPlayer->playUISound(SOUND_error);
            return;
        }

        mouse->RemoveHoldingItem();
        return;
    }
}

bool CmpSkillValue(int valToCompare, CombinedSkillValue skillValue) {
    int val;
    if (valToCompare <= 63)
        val = skillValue.level();
    else
        val = skillValue.joined();
    return val >= valToCompare;
}

//----- (00449BB4) --------------------------------------------------------
bool Character::CompareVariable(VariableType VarNum, int pValue) {
    // in some cases this calls only calls v4 >= pValue, which i've
    // changed to return false, since these values are supposed to
    // be positive and v4 was -1 by default
    Assert(pValue >= 0, "Compare variable shouldn't have negative arguments");

    signed int v4;                         // edi@1
    uint8_t test_bit_value;        // eax@25
    uint8_t byteWithRequestedBit;  // cl@25
    LocationInfo *v19;                   // eax@122
    LocationInfo *v21;                   // eax@126
    int actStat;                           // ebx@161
    int baseStat;                          // eax@161

    if (VarNum >= VAR_MapPersistentVariable_0 && VarNum <= VAR_MapPersistentVariable_74)
        return (uint8_t)engine->_persistentVariables.mapVars[std::to_underlying(VarNum) - std::to_underlying(VAR_MapPersistentVariable_0)] >= pValue;

    // not really sure whether the number gets up to 99, but can't ignore the possibility
    if (VarNum >= VAR_MapPersistentDecorVariable_0 && VarNum <= VAR_MapPersistentDecorVariable_24)
        return (uint8_t)engine->_persistentVariables.decorVars[std::to_underlying(VarNum) - std::to_underlying(VAR_MapPersistentDecorVariable_0)] >= pValue;

    switch (VarNum) {
        case VAR_Sex:
            return (pValue == this->uSex);
        case VAR_Class:
            return (pValue == this->classType);
        case VAR_Race:
            return pValue == GetRace();
        case VAR_CurrentHP:
            return this->health >= pValue;
        case VAR_MaxHP:
            return (this->health >= GetMaxHealth());
        case VAR_CurrentSP:
            return this->mana >= pValue;
        case VAR_MaxSP:
            return (this->mana >= GetMaxMana());
        case VAR_ActualAC:
            return GetActualAC() >= pValue;
        case VAR_ACModifier:
            return this->sACModifier >= pValue;
        case VAR_BaseLevel:
            return this->uLevel >= pValue;
        case VAR_LevelModifier:
            return this->sLevelModifier >= pValue;
        case VAR_Age:
            return GetActualAge() >= (unsigned int)pValue;
        case VAR_Award:
            return _achievedAwardsBits[pValue];
        case VAR_Experience:
            return this->experience >= pValue;  // TODO(_) change pValue to long long
        case VAR_QBits_QuestsDone:
            return pParty->_questBits[pValue];
        case VAR_PlayerItemInHands:
            // for (int i = 0; i < 138; i++)
            for (int i = 0; i < INVENTORY_SLOT_COUNT; i++) {
                if (pInventoryItemList[i].uItemID == ITEM_TYPE(pValue)) {
                    return true;
                }
            }
            return pParty->pPickedItem.uItemID == ITEM_TYPE(pValue);

        case VAR_Hour:
            return pParty->GetPlayingTime().GetHoursOfDay() == pValue;

        case VAR_DayOfYear:
            return pParty->GetPlayingTime().GetDays() % 336 + 1 == pValue;

        case VAR_DayOfWeek:
            return pParty->GetPlayingTime().GetDays() % 7 == pValue;

        case VAR_FixedGold:
            return pParty->GetGold() >= pValue;
        case VAR_FixedFood:
            return pParty->GetFood() >= pValue;
        case VAR_MightBonus:
            return this->uMightBonus >= pValue;
        case VAR_IntellectBonus:
            return this->uIntelligenceBonus >= pValue;
        case VAR_PersonalityBonus:
            return this->uPersonalityBonus >= pValue;
        case VAR_EnduranceBonus:
            return this->uEnduranceBonus >= pValue;
        case VAR_SpeedBonus:
            return this->uSpeedBonus >= pValue;
        case VAR_AccuracyBonus:
            return this->uAccuracyBonus >= pValue;
        case VAR_LuckBonus:
            return this->uLuckBonus >= pValue;
        case VAR_BaseMight:
            return this->uMight >= pValue;
        case VAR_BaseIntellect:
            return this->uIntelligence >= pValue;
        case VAR_BasePersonality:
            return this->uPersonality >= pValue;
        case VAR_BaseEndurance:
            return this->uEndurance >= pValue;
        case VAR_BaseSpeed:
            return this->uSpeed >= pValue;
        case VAR_BaseAccuracy:
            return this->uAccuracy >= pValue;
        case VAR_BaseLuck:
            return this->uLuck >= pValue;
        case VAR_ActualMight:
            return GetActualMight() >= pValue;
        case VAR_ActualIntellect:
            return GetActualIntelligence() >= pValue;
        case VAR_ActualPersonality:
            return GetActualPersonality() >= pValue;
        case VAR_ActualEndurance:
            return GetActualEndurance() >= pValue;
        case VAR_ActualSpeed:
            return GetActualSpeed() >= pValue;
        case VAR_ActualAccuracy:
            return GetActualAccuracy() >= pValue;
        case VAR_ActualLuck:
            return GetActualLuck() >= pValue;
        case VAR_FireResistance:
            return this->sResFireBase >= pValue;
        case VAR_AirResistance:
            return this->sResAirBase >= pValue;
        case VAR_WaterResistance:
            return this->sResWaterBase >= pValue;
        case VAR_EarthResistance:
            return this->sResEarthBase >= pValue;
        case VAR_SpiritResistance:
            return this->sResSpiritBase >= pValue;
        case VAR_MindResistance:
            return this->sResMindBase >= pValue;
        case VAR_BodyResistance:
            return this->sResBodyBase >= pValue;
        case VAR_LightResistance:
            return this->sResLightBase >= pValue;
        case VAR_DarkResistance:
            return this->sResDarkBase >= pValue;
        case VAR_PhysicalResistance:
            Error("Physical resistance isn't used in events");
            return false;
        case VAR_MagicResistance:
            return this->sResMagicBase >= pValue;
        case VAR_FireResistanceBonus:
            return this->sResFireBonus >= pValue;
        case VAR_AirResistanceBonus:
            return this->sResAirBonus >= pValue;
        case VAR_WaterResistanceBonus:
            return this->sResWaterBonus >= pValue;
        case VAR_EarthResistanceBonus:
            return this->sResEarthBonus >= pValue;
        case VAR_SpiritResistanceBonus:
            return this->sResSpiritBonus >= pValue;
        case VAR_MindResistanceBonus:
            return this->sResMindBonus >= pValue;
        case VAR_BodyResistanceBonus:
            return this->sResBodyBonus >= pValue;
        case VAR_LightResistanceBonus:
            return this->sResLightBonus >= pValue;
        case VAR_DarkResistanceBonus:
            return this->sResDarkBonus >= pValue;
        case VAR_MagicResistanceBonus:
            return this->sResMagicBonus >= pValue;
        case VAR_StaffSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_STAFF]);
        case VAR_SwordSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_SWORD]);
        case VAR_DaggerSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_DAGGER]);
        case VAR_AxeSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_AXE]);
        case VAR_SpearSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_SPEAR]);
        case VAR_BowSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_BOW]);
        case VAR_MaceSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_MACE]);
        case VAR_BlasterSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_BLASTER]);
        case VAR_ShieldSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_SHIELD]);
        case VAR_LeatherSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_LEATHER]);
        case VAR_SkillChain:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_CHAIN]);
        case VAR_PlateSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_PLATE]);
        case VAR_FireSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_FIRE]);
        case VAR_AirSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_AIR]);
        case VAR_WaterSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_WATER]);
        case VAR_EarthSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_EARTH]);
        case VAR_SpiritSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_SPIRIT]);
        case VAR_MindSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_MIND]);
        case VAR_BodySkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_BODY]);
        case VAR_LightSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_LIGHT]);
        case VAR_DarkSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_DARK]);
        case VAR_IdentifyItemSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_ITEM_ID]);
        case VAR_MerchantSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_MERCHANT]);
        case VAR_RepairSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_REPAIR]);
        case VAR_BodybuildingSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_BODYBUILDING]);
        case VAR_MeditationSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_MEDITATION]);
        case VAR_PerceptionSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_PERCEPTION]);
        case VAR_DiplomacySkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_DIPLOMACY]);
        case VAR_ThieverySkill:
            //Error("Thievery isn't used in events");
            //return false;
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_THIEVERY]);  // wasn't in the original
        case VAR_DisarmTrapSkill:  // wasn't in the original
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_TRAP_DISARM]);
        case VAR_DodgeSkill:  // wasn't in the original
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_DODGE]);
        case VAR_UnarmedSkill:  // wasn't in the original
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_UNARMED]);
        case VAR_IdentifyMonsterSkill:  // wasn't in the original
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_MONSTER_ID]);
        case VAR_ArmsmasterSkill:  // wasn't in the original
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_ARMSMASTER]);
        case VAR_StealingSkill:  // wasn't in the original
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_STEALING]);
        case VAR_AlchemySkill:  // wasn't in the original
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_ALCHEMY]);
        case VAR_LearningSkill:
            return CmpSkillValue(pValue, this->pActiveSkills[CHARACTER_SKILL_LEARNING]);
        case VAR_Cursed:
            return conditions.Has(CONDITION_CURSED);
        case VAR_Weak:
            return conditions.Has(CONDITION_WEAK);
        case VAR_Asleep:
            return conditions.Has(CONDITION_SLEEP);
        case VAR_Afraid:
            return conditions.Has(CONDITION_FEAR);
        case VAR_Drunk:
            return conditions.Has(CONDITION_DRUNK);
        case VAR_Insane:
            return conditions.Has(CONDITION_INSANE);
        case VAR_PoisonedGreen:
            return conditions.Has(CONDITION_POISON_WEAK);
        case VAR_DiseasedGreen:
            return conditions.Has(CONDITION_DISEASE_WEAK);
        case VAR_PoisonedYellow:
            return conditions.Has(CONDITION_POISON_MEDIUM);
        case VAR_DiseasedYellow:
            return conditions.Has(CONDITION_DISEASE_MEDIUM);
        case VAR_PoisonedRed:
            return conditions.Has(CONDITION_POISON_SEVERE);
        case VAR_DiseasedRed:
            return conditions.Has(CONDITION_DISEASE_SEVERE);
        case VAR_Paralyzed:
            return conditions.Has(CONDITION_PARALYZED);
        case VAR_Unconsious:
            return conditions.Has(CONDITION_UNCONSCIOUS);
        case VAR_Dead:
            return conditions.Has(CONDITION_DEAD);
        case VAR_Stoned:
            return conditions.Has(CONDITION_PETRIFIED);
        case VAR_Eradicated:
            return conditions.Has(CONDITION_ERADICATED);
        case VAR_MajorCondition: {
            Condition condition = GetMajorConditionIdx();
            if (condition != CONDITION_GOOD) {
                return std::to_underlying(condition) >= pValue;
            }
            return true;
        }
        case VAR_AutoNotes:
            return pParty->_autonoteBits[pValue];
        case VAR_IsMightMoreThanBase:
            actStat = GetActualMight();
            baseStat = GetBaseMight();
            return (actStat >= baseStat);
        case VAR_IsIntellectMoreThanBase:
            actStat = GetActualIntelligence();
            baseStat = GetBaseIntelligence();
            return (actStat >= baseStat);
        case VAR_IsPersonalityMoreThanBase:
            actStat = GetActualPersonality();
            baseStat = GetBasePersonality();
            return (actStat >= baseStat);
        case VAR_IsEnduranceMoreThanBase:
            actStat = GetActualEndurance();
            baseStat = GetBaseEndurance();
            return (actStat >= baseStat);
        case VAR_IsSpeedMoreThanBase:
            actStat = GetActualSpeed();
            baseStat = GetBaseSpeed();
            return (actStat >= baseStat);
        case VAR_IsAccuracyMoreThanBase:
            actStat = GetActualAccuracy();
            baseStat = GetBaseAccuracy();
            return (actStat >= baseStat);
        case VAR_IsLuckMoreThanBase:
            actStat = GetActualLuck();
            baseStat = GetBaseLuck();
            return (actStat >= baseStat);
        case VAR_PlayerBits:
            return this->_characterEventBits[pValue];
        case VAR_NPCs2:
            return pNPCStats->pNewNPCData[pValue].Hired();
        case VAR_IsFlying:
            if (pParty->bFlying && pParty->pPartyBuffs[PARTY_BUFF_FLY].Active())
                return true;
            return false;
        case VAR_HiredNPCHasSpeciality:
            return CheckHiredNPCSpeciality((NPCProf)pValue);
        case VAR_CircusPrises:  // isn't used in MM6 since 0x1D6u is a book of
                                // regeneration
            v4 = 0;
            for (Character &character : pParty->pCharacters) {
                for (int invPos = 0; invPos < TOTAL_ITEM_SLOT_COUNT; invPos++) {
                    ITEM_TYPE itemId;

                    if (invPos < INVENTORY_SLOT_COUNT) {
                        itemId = character.pInventoryItemList[invPos].uItemID;
                    } else {
                        itemId = character.pEquippedItems[invPos - INVENTORY_SLOT_COUNT].uItemID;
                    }
                    switch (itemId) {
                        case ITEM_SPELLBOOK_REGENERATION:
                            ++v4;
                            break;
                        case ITEM_SPELLBOOK_CURE_POISON:
                            v4 += 3;
                            break;
                        case ITEM_SPELLBOOK_LIGHT_BOLT:
                            v4 += 5;
                            break;
                        default:
                            break;
                    }
                }
            }
            return v4 >= pValue;
        case VAR_NumSkillPoints:
            return this->uSkillPoints >= (unsigned int)pValue;
        case VAR_MonthIs:
            return (pParty->uCurrentMonth == (unsigned int)pValue);
        case VAR_Counter1:
        case VAR_Counter2:
        case VAR_Counter3:
        case VAR_Counter4:
        case VAR_Counter5:
        case VAR_Counter6:
        case VAR_Counter7:
        case VAR_Counter8:
        case VAR_Counter9:
        case VAR_Counter10:
        {
            int idx = std::to_underlying(VarNum) - std::to_underlying(VAR_Counter1);
            if (pParty->PartyTimes.CounterEventValues[idx ].Valid()) {
                return (pParty->PartyTimes.CounterEventValues[idx] + GameTime::FromHours(pValue)) <= pParty->GetPlayingTime();
            }
            return false;
        }

        case VAR_ReputationInCurrentLocation:
            v19 = &currentLocationInfo();
            return (v19->reputation >= pValue);

        case VAR_Unknown1:
            v21 = &currentLocationInfo();
            return (v21->alertStatus == pValue);  // yes, equality, not >=

        case VAR_GoldInBank:
            return pParty->uNumGoldInBank >= (unsigned int)pValue;

        case VAR_NumDeaths:
            return pParty->uNumDeaths >= (unsigned int)pValue;

        case VAR_NumBounties:
            return pParty->uNumBountiesCollected >= (unsigned int)pValue;

        case VAR_PrisonTerms:
            return pParty->uNumPrisonTerms >= pValue;
        case VAR_ArenaWinsPage:
            return (uint8_t)pParty->uNumArenaWins[0] >= pValue;
        case VAR_ArenaWinsSquire:
            return (uint8_t)pParty->uNumArenaWins[1] >= pValue;
        case VAR_ArenaWinsKnight:
            return (uint8_t)pParty->uNumArenaWins[2] >= pValue;
        case VAR_ArenaWinsLord:
            return pParty->uNumArenaWins[3] >= pValue;
        case VAR_Invisible:
            return pParty->pPartyBuffs[PARTY_BUFF_INVISIBILITY].Active();
        case VAR_ItemEquipped:
            for (ITEM_SLOT i : allItemSlots()) {
                if (HasItemEquipped(i) &&
                    GetNthEquippedIndexItem(i)->uItemID == ITEM_TYPE(pValue)) {
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

//----- (0044A5CB) --------------------------------------------------------
void Character::SetVariable(VariableType var_type, signed int var_value) {
    int gold{}, food{};
    LocationInfo *ddm;
    ItemGen item;

    if (var_type >= VAR_History_0 && var_type <= VAR_History_28) {
        if (!pParty->PartyTimes.HistoryEventTimes[std::to_underlying(var_type) - std::to_underlying(VAR_History_0)]) {
            pParty->PartyTimes.HistoryEventTimes[std::to_underlying(var_type) - std::to_underlying(VAR_History_0)] = pParty->GetPlayingTime();
            if (!pStorylineText->StoreLine[std::to_underlying(var_type) - std::to_underlying(VAR_History_0)].pText.empty()) {
                bFlashHistoryBook = true;
                PlayAwardSound();
            }
        }
        return;
    }

    if (var_type >= VAR_MapPersistentVariable_0 && var_type <= VAR_MapPersistentVariable_74) {
        engine->_persistentVariables.mapVars[std::to_underlying(var_type) - std::to_underlying(VAR_MapPersistentVariable_0)] = (char)var_value;
        return;
    }

    // not really sure whether the number gets up to 99, but can't ignore the possibility
    if (var_type >= VAR_MapPersistentDecorVariable_0 && var_type <= VAR_MapPersistentDecorVariable_24) {
        engine->_persistentVariables.decorVars[std::to_underlying(var_type) - std::to_underlying(VAR_MapPersistentDecorVariable_0)] = (unsigned char)var_value;
        return;
    }

    if (var_type >= VAR_UnknownTimeEvent0 && var_type <= VAR_UnknownTimeEvent19) {
        pParty->PartyTimes._s_times[std::to_underlying(var_type) - std::to_underlying(VAR_UnknownTimeEvent0)] = pParty->GetPlayingTime();
        PlayAwardSound();
        return;
    }

    switch (var_type) {
        case VAR_Sex:
            this->uSex = (CharacterSex)var_value;
            PlayAwardSound_Anim();
            return;
        case VAR_Class:
            this->classType = (CharacterClassType)var_value;
            if ((CharacterClassType)var_value == CHARACTER_CLASS_LICH) {
                for (int i = 0; i < TOTAL_ITEM_SLOT_COUNT; i++) {
                    if (this->pOwnItems[i].uItemID == ITEM_QUEST_LICH_JAR_EMPTY) {
                        this->pOwnItems[i].uItemID = ITEM_QUEST_LICH_JAR_FULL;
                        this->pOwnItems[i].uHolderPlayer = getCharacterIndex();
                    }
                }
                if (this->sResFireBase < 20) this->sResFireBase = 20;
                if (this->sResAirBase < 20) this->sResAirBase = 20;
                if (this->sResWaterBase < 20) this->sResWaterBase = 20;
                if (this->sResEarthBase < 20) this->sResEarthBase = 20;
                this->sResMindBase = 200;
                this->sResBodyBase = 200;
                int sex = this->GetSexByVoice();
                this->uPrevVoiceID = this->uVoiceID;
                this->uPrevFace = this->uCurrentFace;
                if (sex) {
                    this->uCurrentFace = 21;
                    this->uVoiceID = 21;
                } else {
                    this->uCurrentFace = 20;
                    this->uVoiceID = 20;
                }
                GameUI_ReloadPlayerPortraits(getCharacterIndex(),
                                             this->uCurrentFace);
            }
            PlayAwardSound_Anim();
            return;
        case VAR_CurrentHP:
            this->health = var_value;
            PlayAwardSound_Anim();
            return;
        case VAR_MaxHP:
            this->health = GetMaxHealth();
            return;
        case VAR_CurrentSP:
            this->mana = var_value;
            PlayAwardSound_Anim();
            return;
        case VAR_MaxSP:
            this->mana = GetMaxMana();
            return;
        case VAR_ACModifier:
            this->sACModifier = (uint8_t)var_value;
            PlayAwardSound_Anim();
            return;
        case VAR_BaseLevel:
            this->uLevel = (uint8_t)var_value;
            PlayAwardSound_Anim();
            return;
        case VAR_LevelModifier:
            this->sLevelModifier = (uint8_t)var_value;
            PlayAwardSound_Anim();
            return;
        case VAR_Age:
            this->sAgeModifier = var_value;
            return;
        case VAR_Award:
            if (!this->_achievedAwardsBits[var_value] && !pAwards[var_value].pText.empty()) {
                PlayAwardSound_Anim();
                this->playReaction(SPEECH_AWARD_GOT);
            }
            this->_achievedAwardsBits.set(var_value);
            return;
        case VAR_Experience:
            this->experience = var_value;
            PlayAwardSound_Anim();
            return;
        case VAR_QBits_QuestsDone:
            if (!pParty->_questBits[var_value] && !pQuestTable[var_value].empty()) {
                bFlashQuestBook = true;
                spell_fx_renderer->SetPlayerBuffAnim(BECOME_MAGIC_GUILD_MEMBER, getCharacterIndex());
                PlayAwardSound();
                this->playReaction(SPEECH_QUEST_GOT);
            }
            pParty->_questBits.set(var_value);
            return;
        case VAR_PlayerItemInHands:
            item.Reset();
            item.uItemID = ITEM_TYPE(var_value);
            item.uAttributes = ITEM_IDENTIFIED;
            pParty->setHoldingItem(&item);
            if (isSpawnableArtifact(ITEM_TYPE(var_value)))
                pParty->pIsArtifactFound[ITEM_TYPE(var_value)] = true;
            return;
        case VAR_FixedGold:
            pParty->SetGold(var_value);
            return;
        case VAR_RandomGold:
            gold = grng->random(var_value) + 1;
            pParty->SetGold(gold);
            GameUI_SetStatusBar(LSTR_FMT_YOU_HAVE_D_GOLD, gold);
            GameUI_DrawFoodAndGold();
            return;
        case VAR_FixedFood:
            pParty->SetFood(var_value);
            PlayAwardSound_Anim();
            return;
        case VAR_RandomFood:
            food = grng->random(var_value) + 1;
            pParty->SetFood(food);
            GameUI_SetStatusBar(localization->FormatString(LSTR_FMT_YOU_HAVE_D_FOOD, food));
            GameUI_DrawFoodAndGold();
            PlayAwardSound_Anim();
            return;
        case VAR_BaseMight:
            this->uMight = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseIntellect:
            this->uIntelligence = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BasePersonality:
            this->uPersonality = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseEndurance:
            this->uEndurance = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseSpeed:
            this->uSpeed = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseAccuracy:
            this->uAccuracy = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseLuck:
            this->uLuck = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_MightBonus:
        case VAR_ActualMight:
            this->uMightBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_IntellectBonus:
        case VAR_ActualIntellect:
            this->uIntelligenceBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_PersonalityBonus:
        case VAR_ActualPersonality:
            this->uPersonalityBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_EnduranceBonus:
        case VAR_ActualEndurance:
            this->uEnduranceBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_SpeedBonus:
        case VAR_ActualSpeed:
            this->uSpeedBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_AccuracyBonus:
        case VAR_ActualAccuracy:
            this->uAccuracyBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_LuckBonus:
        case VAR_ActualLuck:
            this->uLuckBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_FireResistance:
            this->sResFireBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_AirResistance:
            this->sResAirBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_WaterResistance:
            this->sResWaterBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_EarthResistance:
            this->sResEarthBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_SpiritResistance:
            this->sResSpiritBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_MindResistance:
            this->sResMindBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BodyResistance:
            this->sResBodyBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_LightResistance:
            this->sResLightBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_DarkResistance:
            this->sResDarkBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_MagicResistance:
            this->sResMagicBase = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_FireResistanceBonus:
            this->sResFireBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_AirResistanceBonus:
            this->sResAirBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_WaterResistanceBonus:
            this->sResWaterBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_EarthResistanceBonus:
            this->sResEarthBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_SpiritResistanceBonus:
            this->sResSpiritBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_MindResistanceBonus:
            this->sResMindBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_BodyResistanceBonus:
            this->sResBodyBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_LightResistanceBonus:
            this->sResLightBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_DarkResistanceBonus:
            this->sResDarkBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_PhysicalResistanceBonus:
            Error("Physical res. bonus not used");
            return;
        case VAR_MagicResistanceBonus:
            this->sResMagicBonus = (uint8_t)var_value;
            PlayAwardSound_Anim_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_Cursed:
            this->SetCondition(CONDITION_CURSED, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Weak:
            this->SetCondition(CONDITION_WEAK, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Asleep:
            this->SetCondition(CONDITION_SLEEP, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Afraid:
            this->SetCondition(CONDITION_FEAR, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Drunk:
            this->SetCondition(CONDITION_DRUNK, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Insane:
            this->SetCondition(CONDITION_INSANE, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_PoisonedGreen:
            this->SetCondition(CONDITION_POISON_WEAK, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_DiseasedGreen:
            this->SetCondition(CONDITION_DISEASE_WEAK, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_PoisonedYellow:
            this->SetCondition(CONDITION_POISON_MEDIUM, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_DiseasedYellow:
            this->SetCondition(CONDITION_DISEASE_MEDIUM, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_PoisonedRed:
            this->SetCondition(CONDITION_POISON_SEVERE, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_DiseasedRed:
            this->SetCondition(CONDITION_DISEASE_SEVERE, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Paralyzed:
            this->SetCondition(CONDITION_PARALYZED, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Unconsious:
            this->SetCondition(CONDITION_UNCONSCIOUS, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Dead:
            this->SetCondition(CONDITION_DEAD, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Stoned:
            this->SetCondition(CONDITION_PETRIFIED, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_Eradicated:
            this->SetCondition(CONDITION_ERADICATED, 1);
            PlayAwardSound_Anim();
            return;
        case VAR_MajorCondition:
            conditions.ResetAll();
            PlayAwardSound_Anim();
            return;
        case VAR_AutoNotes:
            assert(var_value > 0);
            if (!pParty->_autonoteBits[var_value] && !pAutonoteTxt[var_value].pText.empty()) {
                spell_fx_renderer->SetPlayerBuffAnim(BECOME_MAGIC_GUILD_MEMBER, getCharacterIndex());
                this->playReaction(SPEECH_AWARD_GOT);
                bFlashAutonotesBook = true;
                autonoteBookDisplayType = pAutonoteTxt[var_value].eType;  // dword_72371C[2 * a3];
            }
            pParty->_autonoteBits.set(var_value);
            PlayAwardSound();
            return;
        case VAR_PlayerBits:
            _characterEventBits.set(var_value);
            return;
        case VAR_NPCs2:
            pParty->hirelingScrollPosition = 0;
            pNPCStats->pNewNPCData[var_value].uFlags |= NPC_HIRED;
            pParty->CountHirelings();
            return;
        case VAR_NumSkillPoints:
            this->uSkillPoints = var_value;
            return;

        case VAR_Counter1:
        case VAR_Counter2:
        case VAR_Counter3:
        case VAR_Counter4:
        case VAR_Counter5:
        case VAR_Counter6:
        case VAR_Counter7:
        case VAR_Counter8:
        case VAR_Counter9:
        case VAR_Counter10:
            pParty->PartyTimes.CounterEventValues[std::to_underlying(var_type) - std::to_underlying(VAR_Counter1)] = pParty->GetPlayingTime();
            return;

        case VAR_ReputationInCurrentLocation:
            ddm = &currentLocationInfo();
            ddm->reputation = var_value;
            if (var_value > 10000)
                ddm->reputation = 10000;
            return;
        case VAR_GoldInBank:
            pParty->uNumGoldInBank = var_value;
            return;
        case VAR_NumDeaths:
            pParty->uNumDeaths = var_value;
            return;
        case VAR_NumBounties:
            pParty->uNumBountiesCollected = var_value;
            return;
        case VAR_PrisonTerms:
            pParty->uNumPrisonTerms = var_value;
            return;
        case VAR_ArenaWinsPage:
            pParty->uNumArenaWins[0] = var_value;
            return;
        case VAR_ArenaWinsSquire:
            pParty->uNumArenaWins[1] = var_value;
            return;
        case VAR_ArenaWinsKnight:
            pParty->uNumArenaWins[2] = var_value;
            return;
        case VAR_ArenaWinsLord:
            pParty->uNumArenaWins[3] = var_value;
            return;
        case VAR_StaffSkill:
            pActiveSkills[CHARACTER_SKILL_STAFF] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_SwordSkill:
            pActiveSkills[CHARACTER_SKILL_SWORD] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_DaggerSkill:
            pActiveSkills[CHARACTER_SKILL_DAGGER] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_AxeSkill:
            pActiveSkills[CHARACTER_SKILL_AXE] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_SpearSkill:
            pActiveSkills[CHARACTER_SKILL_SPEAR] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_BowSkill:
            pActiveSkills[CHARACTER_SKILL_BOW] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_MaceSkill:
            pActiveSkills[CHARACTER_SKILL_MACE] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_BlasterSkill:
            pActiveSkills[CHARACTER_SKILL_BLASTER] = CombinedSkillValue::fromJoined(var_value);;
            SetSkillReaction();
            return;
        case VAR_ShieldSkill:
            pActiveSkills[CHARACTER_SKILL_SHIELD] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_LeatherSkill:
            pActiveSkills[CHARACTER_SKILL_LEATHER] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_SkillChain:
            pActiveSkills[CHARACTER_SKILL_CHAIN] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_PlateSkill:
            pActiveSkills[CHARACTER_SKILL_PLATE] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_FireSkill:
            pActiveSkills[CHARACTER_SKILL_FIRE] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_AirSkill:
            pActiveSkills[CHARACTER_SKILL_AIR] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_WaterSkill:
            pActiveSkills[CHARACTER_SKILL_WATER] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_EarthSkill:
            pActiveSkills[CHARACTER_SKILL_EARTH] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_SpiritSkill:
            pActiveSkills[CHARACTER_SKILL_SPIRIT] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_MindSkill:
            pActiveSkills[CHARACTER_SKILL_MIND] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_BodySkill:
            pActiveSkills[CHARACTER_SKILL_BODY] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_LightSkill:
            pActiveSkills[CHARACTER_SKILL_LIGHT] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_DarkSkill:
            pActiveSkills[CHARACTER_SKILL_DARK] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_IdentifyItemSkill:
            pActiveSkills[CHARACTER_SKILL_ITEM_ID] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_MerchantSkill:
            pActiveSkills[CHARACTER_SKILL_MERCHANT] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_RepairSkill:
            pActiveSkills[CHARACTER_SKILL_REPAIR] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_BodybuildingSkill:
            pActiveSkills[CHARACTER_SKILL_BODYBUILDING] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_MeditationSkill:
            pActiveSkills[CHARACTER_SKILL_MEDITATION] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_PerceptionSkill:
            pActiveSkills[CHARACTER_SKILL_PERCEPTION] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_DiplomacySkill:
            pActiveSkills[CHARACTER_SKILL_DIPLOMACY] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_ThieverySkill:
            Error("Thieving unsupported");
            return;
        case VAR_DisarmTrapSkill:
            pActiveSkills[CHARACTER_SKILL_TRAP_DISARM] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_DodgeSkill:
            pActiveSkills[CHARACTER_SKILL_DODGE] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_UnarmedSkill:
            pActiveSkills[CHARACTER_SKILL_UNARMED] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_IdentifyMonsterSkill:
            pActiveSkills[CHARACTER_SKILL_MONSTER_ID] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_ArmsmasterSkill:
            pActiveSkills[CHARACTER_SKILL_ARMSMASTER] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_StealingSkill:
            pActiveSkills[CHARACTER_SKILL_STEALING] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_AlchemySkill:
            pActiveSkills[CHARACTER_SKILL_ALCHEMY] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        case VAR_LearningSkill:
            pActiveSkills[CHARACTER_SKILL_LEARNING] = CombinedSkillValue::fromJoined(var_value);
            SetSkillReaction();
            return;
        default:
            return;
    }
}

//----- (new function) --------------------------------------------------------
void Character::PlayAwardSound() {
    //int playerIndex = getCharacterIndex();
    //int v25 = PID(OBJECT_Character, playerIndex + 48);
    //pAudioPlayer->playSound(SOUND_quest, v25);
    pAudioPlayer->playUISound(SOUND_quest);
}

//----- (new function) --------------------------------------------------------
void Character::PlayAwardSound_Anim() {
    int playerIndex = getCharacterIndex();
    spell_fx_renderer->SetPlayerBuffAnim(BECOME_MAGIC_GUILD_MEMBER, playerIndex);
    PlayAwardSound();
}

//----- (new function) --------------------------------------------------------
void Character::PlayAwardSound_Anim_Face(CharacterSpeech speech) {
    this->playReaction(speech);
    PlayAwardSound_Anim();
}

//----- (new function) --------------------------------------------------------
void Character::SetSkillReaction() {
    int playerIndex = getCharacterIndex();
    spell_fx_renderer->SetPlayerBuffAnim(BECOME_MAGIC_GUILD_MEMBER, playerIndex);
    PlayAwardSound();
}

//----- (0044AFFB) --------------------------------------------------------
void Character::AddVariable(VariableType var_type, signed int val) {
    int food{};
    LocationInfo *ddm;
    ItemGen item;

    if (var_type >= VAR_Counter1 && var_type <= VAR_Counter10) {
        pParty->PartyTimes.CounterEventValues[std::to_underlying(var_type) - std::to_underlying(VAR_Counter1)] = pParty->GetPlayingTime();
        return;
    }

    if (var_type >= VAR_UnknownTimeEvent0 && var_type <= VAR_UnknownTimeEvent19) {
        pParty->PartyTimes._s_times[std::to_underlying(var_type) - std::to_underlying(VAR_UnknownTimeEvent0)] = pParty->GetPlayingTime();
        PlayAwardSound();
        return;
    }

    if (var_type >= VAR_MapPersistentVariable_0 && var_type <= VAR_MapPersistentVariable_74) {
        if (255 - val > engine->_persistentVariables.mapVars[std::to_underlying(var_type) - std::to_underlying(VAR_MapPersistentVariable_0)]) {
            engine->_persistentVariables.mapVars[std::to_underlying(var_type) - std::to_underlying(VAR_MapPersistentVariable_0)] += val;
        } else {
            engine->_persistentVariables.mapVars[std::to_underlying(var_type) - std::to_underlying(VAR_MapPersistentVariable_0)] = 255;
        }
        return;
    }
    if (var_type >= VAR_MapPersistentDecorVariable_0 && var_type <= VAR_MapPersistentDecorVariable_24) {
        if (255 - val > engine->_persistentVariables.decorVars[std::to_underlying(var_type) - std::to_underlying(VAR_MapPersistentDecorVariable_0)]) {
            engine->_persistentVariables.decorVars[std::to_underlying(var_type) - std::to_underlying(VAR_MapPersistentDecorVariable_0)] += val;
        } else {
            engine->_persistentVariables.decorVars[std::to_underlying(var_type) - std::to_underlying(VAR_MapPersistentDecorVariable_0)] = 255;
        }
        return;
    }

    if (var_type >= VAR_History_0 && var_type <= VAR_History_28) {
        if (!pParty->PartyTimes.HistoryEventTimes[std::to_underlying(var_type) - std::to_underlying(VAR_History_0)]) {
            pParty->PartyTimes.HistoryEventTimes[std::to_underlying(var_type) - std::to_underlying(VAR_History_0)] = pParty->GetPlayingTime();
            if (!pStorylineText->StoreLine[std::to_underlying(var_type) - std::to_underlying(VAR_History_0)].pText.empty()) {
                bFlashHistoryBook = true;
                PlayAwardSound();
            }
        }
        return;
    }

    switch (var_type) {
        case VAR_RandomGold:
            if (val == 0) val = 1;
            pParty->partyFindsGold(grng->random(val) + 1, GOLD_RECEIVE_NOSHARE_MSG);
            GameUI_DrawFoodAndGold();
            return;
        case VAR_RandomFood:
            if (val == 0) val = 1;
            food = grng->random(val) + 1;
            pParty->GiveFood(food);
            GameUI_SetStatusBar(LSTR_FMT_YOU_FIND_D_FOOD, food);
            GameUI_DrawFoodAndGold();
            PlayAwardSound();
            return;
        case VAR_Sex:
            this->uSex = (CharacterSex)val;
            PlayAwardSound_Anim97();
            return;
        case VAR_Class:
            this->classType = (CharacterClassType)val;
            PlayAwardSound_Anim97();
            return;
        case VAR_CurrentHP:
            this->health = std::min(this->health + val, this->GetMaxHealth());
            PlayAwardSound_Anim97();
            return;
        case VAR_MaxHP:
            this->_health_related = 0;
            this->uFullHealthBonus = 0;
            this->health = this->GetMaxHealth();
            return;
        case VAR_CurrentSP:
            this->mana = std::min(this->mana + val, this->GetMaxMana());
            PlayAwardSound_Anim97();
            return;
        case VAR_MaxSP:
            this->_mana_related = 0;
            this->uFullManaBonus = 0;
            this->mana = GetMaxMana();
            return;
        case VAR_ACModifier:
            this->sACModifier = std::min(this->sACModifier + val, 255);
            PlayAwardSound_Anim97();
            return;
        case VAR_BaseLevel:
            this->uLevel = std::min(this->uLevel + val, 255);
            PlayAwardSound_Anim97();
            return;
        case VAR_LevelModifier:
            this->sLevelModifier = std::min(this->sLevelModifier + val, 255);
            PlayAwardSound_Anim97();
            return;
        case VAR_Age:
            this->sAgeModifier += val;
            return;
        case VAR_Award:
            if (this->_achievedAwardsBits[val] && !pAwards[val].pText.empty()) {
                PlayAwardSound_Anim97_Face(SPEECH_AWARD_GOT);
            }
            this->_achievedAwardsBits.set(val);
            return;
        case VAR_Experience:
            this->experience = std::min((uint64_t)(this->experience + val), UINT64_C(4000000000));
            PlayAwardSound_Anim97();
            return;
        case VAR_QBits_QuestsDone:
            if (!pParty->_questBits[val] && !pQuestTable[val].empty()) {
                bFlashQuestBook = true;
                PlayAwardSound_Anim97_Face(SPEECH_QUEST_GOT);
            }
            pParty->_questBits.set(val);
            return;
        case VAR_PlayerItemInHands:
            item.Reset();
            item.uAttributes = ITEM_IDENTIFIED;
            item.uItemID = ITEM_TYPE(val);
            if (isSpawnableArtifact(ITEM_TYPE(val))) {
                pParty->pIsArtifactFound[ITEM_TYPE(val)] = true;
            } else if (isWand(ITEM_TYPE(val))) {
                item.uNumCharges = grng->random(6) + item.GetDamageMod() + 1;
                item.uMaxCharges = item.uNumCharges;
            }
            pParty->setHoldingItem(&item);
            return;
        case VAR_FixedGold:
            pParty->partyFindsGold(val, GOLD_RECEIVE_NOSHARE_MSG);
            return;
        case VAR_BaseMight:
            this->uMight = std::min(this->uMight + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseIntellect:
            this->uIntelligence = std::min(this->uIntelligence + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BasePersonality:
            this->uPersonality = std::min(this->uPersonality + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseEndurance:
            this->uEndurance = std::min(this->uEndurance + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseSpeed:
            this->uSpeed = std::min(this->uSpeed + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseAccuracy:
            this->uAccuracy = std::min(this->uAccuracy + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseLuck:
            this->uLuck = std::min(this->uLuck + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_FixedFood:
            pParty->GiveFood(val);
            GameUI_SetStatusBar(LSTR_FMT_YOU_FIND_D_FOOD, val);
            PlayAwardSound();
            return;
        case VAR_MightBonus:
        case VAR_ActualMight:
            this->uMightBonus = std::min(this->uMightBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_IntellectBonus:
        case VAR_ActualIntellect:
            this->uIntelligenceBonus = std::min(this->uIntelligenceBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_PersonalityBonus:
        case VAR_ActualPersonality:
            this->uPersonalityBonus = std::min(this->uPersonalityBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_EnduranceBonus:
        case VAR_ActualEndurance:
            this->uEnduranceBonus = std::min(this->uEnduranceBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_SpeedBonus:
        case VAR_ActualSpeed:
            this->uSpeedBonus = std::min(this->uSpeedBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_AccuracyBonus:
        case VAR_ActualAccuracy:
            this->uAccuracyBonus = std::min(this->uAccuracyBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_LuckBonus:
        case VAR_ActualLuck:
            this->uLuckBonus = std::min(this->uLuckBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_FireResistance:
            this->sResFireBase = std::min(this->sResFireBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_AirResistance:
            this->sResAirBase = std::min(this->sResAirBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_WaterResistance:
            this->sResWaterBase = std::min(this->sResWaterBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_EarthResistance:
            this->sResEarthBase = std::min(this->sResEarthBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_SpiritResistance:
            this->sResSpiritBase = std::min(this->sResSpiritBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_MindResistance:
            this->sResMindBase = std::min(this->sResMindBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BodyResistance:
            this->sResBodyBase = std::min(this->sResBodyBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_LightResistance:
            this->sResLightBase = std::min(this->sResLightBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_DarkResistance:
            this->sResDarkBase = std::min(this->sResDarkBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_MagicResistance:
            this->sResMagicBase = std::min(this->sResMagicBase + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_FireResistanceBonus:
            this->sResFireBonus = std::min(this->sResFireBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_AirResistanceBonus:
            this->sResAirBonus = std::min(this->sResAirBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_WaterResistanceBonus:
            this->sResWaterBonus = std::min(this->sResWaterBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_EarthResistanceBonus:
            this->sResEarthBonus = std::min(this->sResEarthBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_SpiritResistanceBonus:
            this->sResSpiritBonus = std::min(this->sResSpiritBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_MindResistanceBonus:
            this->sResMindBonus = std::min(this->sResMindBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_BodyResistanceBonus:
            this->sResBodyBonus = std::min(this->sResBodyBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_LightResistanceBonus:
            this->sResLightBonus = std::min(this->sResLightBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_DarkResistanceBonus:
            this->sResDarkBonus = std::min(this->sResDarkBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_MagicResistanceBonus:
            this->sResMagicBonus = std::min(this->sResMagicBonus + val, 255);
            PlayAwardSound_Anim97_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_Cursed:
            this->SetCondition(CONDITION_CURSED, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Weak:
            this->SetCondition(CONDITION_WEAK, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Asleep:
            this->SetCondition(CONDITION_SLEEP, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Afraid:
            this->SetCondition(CONDITION_FEAR, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Drunk:
            this->SetCondition(CONDITION_DRUNK, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Insane:
            this->SetCondition(CONDITION_INSANE, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_PoisonedGreen:
            this->SetCondition(CONDITION_POISON_WEAK, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_DiseasedGreen:
            this->SetCondition(CONDITION_DISEASE_WEAK, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_PoisonedYellow:
            this->SetCondition(CONDITION_POISON_MEDIUM, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_DiseasedYellow:
            this->SetCondition(CONDITION_DISEASE_MEDIUM, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_PoisonedRed:
            this->SetCondition(CONDITION_POISON_SEVERE, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_DiseasedRed:
            this->SetCondition(CONDITION_DISEASE_SEVERE, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Paralyzed:
            this->SetCondition(CONDITION_PARALYZED, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Unconsious:
            this->SetCondition(CONDITION_UNCONSCIOUS, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Dead:
            this->SetCondition(CONDITION_DEAD, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Stoned:
            this->SetCondition(CONDITION_PETRIFIED, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_Eradicated:
            this->SetCondition(CONDITION_ERADICATED, 1);
            PlayAwardSound_Anim97();
            return;
        case VAR_MajorCondition:
            conditions.ResetAll();
            PlayAwardSound_Anim97();
            return;
        case VAR_AutoNotes:
            assert(val > 0);
            if (!pParty->_autonoteBits[val] && !pAutonoteTxt[val].pText.empty()) {
                this->playReaction(SPEECH_AWARD_GOT);
                bFlashAutonotesBook = true;
                autonoteBookDisplayType = pAutonoteTxt[val].eType;
                spell_fx_renderer->SetPlayerBuffAnim(SPELL_QUEST_COMPLETED, getCharacterIndex());
            }
            pParty->_autonoteBits.set(val);
            PlayAwardSound();
            return;
        case VAR_PlayerBits:
            _characterEventBits.set(val);
            return;
        case VAR_NPCs2:
            pParty->hirelingScrollPosition = 0;
            pNPCStats->pNewNPCData[val].uFlags |= NPC_HIRED;
            pParty->CountHirelings();
            return;
        case VAR_NumSkillPoints:
            this->uSkillPoints += val;
            return;
        case VAR_ReputationInCurrentLocation:
            ddm = &currentLocationInfo();
            ddm->reputation += val;
            if (ddm->reputation > 10000)
                ddm->reputation = 10000;
            return;
        case VAR_GoldInBank:
            pParty->uNumGoldInBank += val;
            return;
        case VAR_NumDeaths:
            pParty->uNumDeaths += val;
            return;
        case VAR_NumBounties:
            pParty->uNumBountiesCollected += val;
            return;
        case VAR_PrisonTerms:
            pParty->uNumPrisonTerms += val;
            return;
        case VAR_ArenaWinsPage:
            pParty->uNumArenaWins[0] += val;
            return;
        case VAR_ArenaWinsSquire:
            pParty->uNumArenaWins[1] += val;
            return;
        case VAR_ArenaWinsKnight:
            pParty->uNumArenaWins[2] += val;
            return;
        case VAR_ArenaWinsLord:
            pParty->uNumArenaWins[3] += val;
            return;
        case VAR_StaffSkill:
            AddSkillByEvent(CHARACTER_SKILL_STAFF, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_SwordSkill:
            AddSkillByEvent(CHARACTER_SKILL_SWORD, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_DaggerSkill:
            AddSkillByEvent(CHARACTER_SKILL_DAGGER, val);;
            PlayAwardSound_Anim97();
            return;
        case VAR_AxeSkill:
            AddSkillByEvent(CHARACTER_SKILL_AXE, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_SpearSkill:
            AddSkillByEvent(CHARACTER_SKILL_SPEAR, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_BowSkill:
            AddSkillByEvent(CHARACTER_SKILL_BOW, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_MaceSkill:
            AddSkillByEvent(CHARACTER_SKILL_MACE, val);;
            PlayAwardSound_Anim97();
            return;
        case VAR_BlasterSkill:
            AddSkillByEvent(CHARACTER_SKILL_BLASTER, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_ShieldSkill:
            AddSkillByEvent(CHARACTER_SKILL_SHIELD, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_LeatherSkill:
            AddSkillByEvent(CHARACTER_SKILL_LEATHER, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_SkillChain:
            AddSkillByEvent(CHARACTER_SKILL_CHAIN, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_PlateSkill:
            AddSkillByEvent(CHARACTER_SKILL_PLATE, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_FireSkill:
            AddSkillByEvent(CHARACTER_SKILL_FIRE, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_AirSkill:
            AddSkillByEvent(CHARACTER_SKILL_AIR, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_WaterSkill:
            AddSkillByEvent(CHARACTER_SKILL_WATER, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_EarthSkill:
            AddSkillByEvent(CHARACTER_SKILL_EARTH, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_SpiritSkill:
            AddSkillByEvent(CHARACTER_SKILL_SPIRIT, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_MindSkill:
            AddSkillByEvent(CHARACTER_SKILL_MIND, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_BodySkill:
            AddSkillByEvent(CHARACTER_SKILL_BODY, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_LightSkill:
            AddSkillByEvent(CHARACTER_SKILL_LIGHT, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_DarkSkill:
            AddSkillByEvent(CHARACTER_SKILL_DARK, val);;
            PlayAwardSound_Anim97();
            return;
        case VAR_IdentifyItemSkill:
            AddSkillByEvent(CHARACTER_SKILL_ITEM_ID, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_MerchantSkill:
            AddSkillByEvent(CHARACTER_SKILL_MERCHANT, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_RepairSkill:
            AddSkillByEvent(CHARACTER_SKILL_REPAIR, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_BodybuildingSkill:
            AddSkillByEvent(CHARACTER_SKILL_BODYBUILDING, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_MeditationSkill:
            AddSkillByEvent(CHARACTER_SKILL_MEDITATION, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_PerceptionSkill:
            AddSkillByEvent(CHARACTER_SKILL_PERCEPTION, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_DiplomacySkill:
            AddSkillByEvent(CHARACTER_SKILL_DIPLOMACY, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_ThieverySkill:
            Error("Thieving unsupported");
            return;
        case VAR_DisarmTrapSkill:
            AddSkillByEvent(CHARACTER_SKILL_TRAP_DISARM, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_DodgeSkill:
            AddSkillByEvent(CHARACTER_SKILL_DODGE, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_UnarmedSkill:
            AddSkillByEvent(CHARACTER_SKILL_UNARMED, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_IdentifyMonsterSkill:
            AddSkillByEvent(CHARACTER_SKILL_MONSTER_ID, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_ArmsmasterSkill:
            AddSkillByEvent(CHARACTER_SKILL_ARMSMASTER, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_StealingSkill:
            AddSkillByEvent(CHARACTER_SKILL_STEALING, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_AlchemySkill:
            AddSkillByEvent(CHARACTER_SKILL_ALCHEMY, val);
            PlayAwardSound_Anim97();
            return;
        case VAR_LearningSkill:
            AddSkillByEvent(CHARACTER_SKILL_LEARNING, val);
            PlayAwardSound_Anim97();
            return;
        default:
            return;
    }
}

//----- (new function) --------------------------------------------------------
void Character::PlayAwardSound_Anim97() {
    int playerIndex = getCharacterIndex();
    spell_fx_renderer->SetPlayerBuffAnim(SPELL_QUEST_COMPLETED, playerIndex);
    PlayAwardSound();
}

//----- (new function) --------------------------------------------------------
void Character::PlayAwardSound_Anim97_Face(CharacterSpeech speech) {
    this->playReaction(speech);
    PlayAwardSound_Anim97();
}

//----- (new function) --------------------------------------------------------
void Character::AddSkillByEvent(CharacterSkillType skill, uint16_t addSkillValue) {
    auto [addLevel, addMastery] = CombinedSkillValue::fromJoinedUnchecked(addSkillValue);

    int newLevel = pActiveSkills[skill].level() + addLevel;
    CharacterSkillMastery newMastery = std::max(pActiveSkills[skill].mastery(), addMastery);

    pActiveSkills[skill] = CombinedSkillValue(newLevel, newMastery);
}

//----- (0044B9C4) --------------------------------------------------------
void Character::SubtractVariable(VariableType VarNum, signed int pValue) {
    LocationInfo *locationHeader;  // eax@90
    int randGold;
    int randFood;
    int npcIndex;

    if (VarNum >= VAR_MapPersistentVariable_0 && VarNum <= VAR_MapPersistentVariable_74) {
        engine->_persistentVariables.mapVars[std::to_underlying(VarNum) - std::to_underlying(VAR_MapPersistentVariable_0)] -= pValue;
        return;
    }
    if (VarNum >= VAR_MapPersistentDecorVariable_0 && VarNum <= VAR_MapPersistentDecorVariable_24) {
        engine->_persistentVariables.decorVars[std::to_underlying(VarNum) - std::to_underlying(VAR_MapPersistentDecorVariable_0)] -= pValue;
        return;
    }

    switch (VarNum) {
        case VAR_CurrentHP:
            receiveDamage((signed int)pValue, DMGT_PHISYCAL);
            PlayAwardSound_Anim98();
            return;
        case VAR_CurrentSP:
            this->mana = std::max(this->mana - pValue, 0);
            PlayAwardSound_Anim98();
            return;
        case VAR_ACModifier:
            this->sACModifier -= (uint8_t)pValue;
            PlayAwardSound_Anim98();
            return;
        case VAR_BaseLevel:
            this->uLevel -= (uint8_t)pValue;
            PlayAwardSound_Anim98();
            return;
        case VAR_LevelModifier:
            this->sLevelModifier -= (uint8_t)pValue;
            PlayAwardSound_Anim98();
            return;
        case VAR_Age:
            this->sAgeModifier -= (int16_t)pValue;
            return;
        case VAR_Award:
            this->_achievedAwardsBits.reset(pValue);
            return;
        case VAR_Experience:
            this->experience -= pValue;
            PlayAwardSound_Anim98();
            return;
        case VAR_QBits_QuestsDone:
            pParty->_questBits.reset(pValue);
            this->playReaction(SPEECH_AWARD_GOT);
            return;
        case VAR_PlayerItemInHands:
            for (ITEM_SLOT i : allItemSlots()) {
                int id_ = this->pEquipment.pIndices[i];
                if (id_ > 0) {
                    if (this->pInventoryItemList[this->pEquipment.pIndices[i] -
                                                 1]
                            .uItemID == ITEM_TYPE(pValue)) {
                        this->pEquipment.pIndices[i] = 0;
                    }
                }
            }
            for (int i = 0; i < INVENTORY_SLOT_COUNT; i++) {
                int id_ = this->pInventoryMatrix[i];
                if (id_ > 0) {
                    if (this->pInventoryItemList[id_ - 1].uItemID == ITEM_TYPE(pValue)) {
                        RemoveItemAtInventoryIndex(i);
                        return;
                    }
                }
            }
            if (pParty->pPickedItem.uItemID == ITEM_TYPE(pValue)) {
                mouse->RemoveHoldingItem();
                return;
            }
            return;
        case VAR_FixedGold:
            if (pValue > pParty->GetGold()) {
                dword_5B65C4_cancelEventProcessing = 1;
                return;
            }
            pParty->TakeGold(pValue);
            return;
        case VAR_RandomGold:
            randGold = grng->random(pValue) + 1;
            if (randGold > pParty->GetGold())
                randGold = pParty->GetGold();
            pParty->TakeGold(randGold);
            GameUI_SetStatusBar(LSTR_FMT_YOU_LOSE_D_GOLD, randGold);
            GameUI_DrawFoodAndGold();
            return;
        case VAR_FixedFood:
            pParty->TakeFood(pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_RandomFood:
            randFood = grng->random(pValue) + 1;
            if (randFood > pParty->GetFood())
                randFood = pParty->GetFood();
            pParty->TakeFood(randFood);
            GameUI_SetStatusBar(LSTR_FMT_YOU_LOSE_D_FOOD, randFood);
            GameUI_DrawFoodAndGold();
            PlayAwardSound_Anim98();
            return;
        case VAR_MightBonus:
        case VAR_ActualMight:
            this->uMightBonus -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_IntellectBonus:
        case VAR_ActualIntellect:
            this->uIntelligenceBonus -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_PersonalityBonus:
        case VAR_ActualPersonality:
            this->uPersonalityBonus -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_EnduranceBonus:
        case VAR_ActualEndurance:
            this->uEnduranceBonus -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_SpeedBonus:
        case VAR_ActualSpeed:
            this->uSpeedBonus -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_AccuracyBonus:
        case VAR_ActualAccuracy:
            this->uAccuracyBonus -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_LuckBonus:
        case VAR_ActualLuck:
            this->uLuckBonus -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_BaseMight:
            this->uMight -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseIntellect:
            this->uIntelligence -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BasePersonality:
            this->uPersonality -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseEndurance:
            this->uEndurance -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseSpeed:
            this->uSpeed -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseAccuracy:
            this->uAccuracy -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BaseLuck:
            this->uLuck -= (uint16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_FireResistance:
            this->sResFireBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_AirResistance:
            this->sResAirBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_WaterResistance:
            this->sResWaterBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_EarthResistance:
            this->sResEarthBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_SpiritResistance:
            this->sResSpiritBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_MindResistance:
            this->sResMindBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_BodyResistance:
            this->sResBodyBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_LightResistance:
            this->sResLightBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_DarkResistance:
            this->sResDarkBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_MagicResistance:
            this->sResMagicBase -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_FireResistanceBonus:
            this->sResFireBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_AirResistanceBonus:
            this->sResAirBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BASE_INC);
            return;
        case VAR_WaterResistanceBonus:
            this->sResWaterBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_EarthResistanceBonus:
            this->sResEarthBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_SpiritResistanceBonus:
            this->sResSpiritBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_MindResistanceBonus:
            this->sResMindBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_BodyResistanceBonus:
            this->sResBodyBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_LightResistanceBonus:
            this->sResLightBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_DarkResistanceBonus:
            this->sResDarkBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_MagicResistanceBonus:
            this->sResMagicBonus -= (int16_t)pValue;
            this->PlayAwardSound_Anim98_Face(SPEECH_STAT_BONUS_INC);
            return;
        case VAR_StaffSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_STAFF, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_SwordSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_SWORD, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_DaggerSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_DAGGER, pValue);;
            PlayAwardSound_Anim98();
            return;
        case VAR_AxeSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_AXE, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_SpearSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_BOW, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_BowSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_BOW, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_MaceSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_MACE, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_BlasterSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_BLASTER, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_ShieldSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_SHIELD, pValue);;
            PlayAwardSound_Anim98();
            return;
        case VAR_LeatherSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_LEATHER, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_SkillChain:
            SubtractSkillByEvent(CHARACTER_SKILL_CHAIN, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_PlateSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_PLATE, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_FireSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_FIRE, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_AirSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_AIR, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_WaterSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_WATER, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_EarthSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_EARTH, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_SpiritSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_SPIRIT, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_MindSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_MIND, pValue);;
            PlayAwardSound_Anim98();
            return;
        case VAR_BodySkill:
            SubtractSkillByEvent(CHARACTER_SKILL_BODY, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_LightSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_LIGHT, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_DarkSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_DARK, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_IdentifyItemSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_ITEM_ID, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_MerchantSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_MERCHANT, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_RepairSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_REPAIR, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_BodybuildingSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_BODYBUILDING, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_MeditationSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_MEDITATION, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_PerceptionSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_PERCEPTION, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_DiplomacySkill:
            SubtractSkillByEvent(CHARACTER_SKILL_DIPLOMACY, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_ThieverySkill:
            Error("Thieving unsupported");
            return;
        case VAR_DisarmTrapSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_TRAP_DISARM, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_DodgeSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_DODGE, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_UnarmedSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_UNARMED, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_IdentifyMonsterSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_MONSTER_ID, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_ArmsmasterSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_ARMSMASTER, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_StealingSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_STEALING, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_AlchemySkill:
            SubtractSkillByEvent(CHARACTER_SKILL_ALCHEMY, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_LearningSkill:
            SubtractSkillByEvent(CHARACTER_SKILL_LEARNING, pValue);
            PlayAwardSound_Anim98();
            return;
        case VAR_Cursed:
            this->conditions.Reset(CONDITION_CURSED);
            PlayAwardSound_Anim98();
            return;
        case VAR_Weak:
            this->conditions.Reset(CONDITION_WEAK);
            PlayAwardSound_Anim98();
            return;
        case VAR_Asleep:
            this->conditions.Reset(CONDITION_SLEEP);
            PlayAwardSound_Anim98();
            return;
        case VAR_Afraid:
            this->conditions.Reset(CONDITION_FEAR);
            PlayAwardSound_Anim98();
            return;
        case VAR_Drunk:
            this->conditions.Reset(CONDITION_DRUNK);
            PlayAwardSound_Anim98();
            return;
        case VAR_Insane:
            this->conditions.Reset(CONDITION_INSANE);
            PlayAwardSound_Anim98();
            return;
        case VAR_PoisonedGreen:
            this->conditions.Reset(CONDITION_POISON_WEAK);
            PlayAwardSound_Anim98();
            return;
        case VAR_DiseasedGreen:
            this->conditions.Reset(CONDITION_DISEASE_WEAK);
            PlayAwardSound_Anim98();
            return;
        case VAR_PoisonedYellow:
            this->conditions.Reset(CONDITION_POISON_MEDIUM);
            PlayAwardSound_Anim98();
            return;
        case VAR_DiseasedYellow:
            this->conditions.Reset(CONDITION_DISEASE_MEDIUM);
            PlayAwardSound_Anim98();
            return;
        case VAR_PoisonedRed:
            this->conditions.Reset(CONDITION_POISON_SEVERE);
            PlayAwardSound_Anim98();
            return;
        case VAR_DiseasedRed:
            this->conditions.Reset(CONDITION_DISEASE_SEVERE);
            PlayAwardSound_Anim98();
            return;
        case VAR_Paralyzed:
            this->conditions.Reset(CONDITION_PARALYZED);
            PlayAwardSound_Anim98();
            return;
        case VAR_Unconsious:
            this->conditions.Reset(CONDITION_UNCONSCIOUS);
            PlayAwardSound_Anim98();
            return;
        case VAR_Dead:
            this->conditions.Reset(CONDITION_DEAD);
            PlayAwardSound_Anim98();
            return;
        case VAR_Stoned:
            this->conditions.Reset(CONDITION_PETRIFIED);
            PlayAwardSound_Anim98();
            return;
        case VAR_Eradicated:
            this->conditions.Reset(CONDITION_ERADICATED);
            PlayAwardSound_Anim98();
            return;
        case VAR_AutoNotes:
            // TODO(Nik-RE-dev): decreasing 1 seems wrong, also bits indexing was changed
            assert(false);
            //pParty->_autonoteBits.reset(pValue - 1);
            return;
        case VAR_PlayerBits:
            _characterEventBits.reset(pValue);
            return;
        case VAR_NPCs2:
            npcIndex = 0;
            GetNewNPCData(sDialogue_SpeakingActorNPC_ID, &npcIndex);
            if (npcIndex == pValue) {
                npcIdToDismissAfterDialogue = pValue;
            } else {
                npcIdToDismissAfterDialogue = 0;
                pParty->hirelingScrollPosition = 0;
                pNPCStats->pNewNPCData[(int)pValue].uFlags &= ~NPC_HIRED;
                pParty->CountHirelings();
            }
            return;
        case VAR_HiredNPCHasSpeciality:
            for (unsigned int i = 0; i < pNPCStats->uNumNewNPCs; i++) {
                if (pNPCStats->pNewNPCData[i].profession == (NPCProf)pValue) {
                    pNPCStats->pNewNPCData[(int)pValue].uFlags &= ~NPC_HIRED;
                }
            }
            if (pParty->pHirelings[0].profession == (NPCProf)pValue) {
                pParty->pHirelings[0] = NPCData();
            }
            if (pParty->pHirelings[1].profession == (NPCProf)pValue) {
                pParty->pHirelings[1] = NPCData();
            }
            pParty->hirelingScrollPosition = 0;
            pParty->CountHirelings();
            return;
        case VAR_NumSkillPoints:
            if ((unsigned int)pValue <= this->uSkillPoints) {
                this->uSkillPoints -= pValue;
            } else {
                this->uSkillPoints = 0;
            }
            return;
        case VAR_ReputationInCurrentLocation:
            locationHeader = &currentLocationInfo();
            locationHeader->reputation -= pValue;
            if (locationHeader->reputation < -10000)
                locationHeader->reputation = -10000;
            return;
        case VAR_GoldInBank:
            if ((unsigned int)pValue <= pParty->uNumGoldInBank) {
                pParty->uNumGoldInBank -= (unsigned int)pValue;
            } else {
                dword_5B65C4_cancelEventProcessing = 1;
            }
            return;
        case VAR_NumDeaths:
            pParty->uNumDeaths -= (unsigned int)pValue;
            return;
        case VAR_NumBounties:
            pParty->uNumBountiesCollected -= (unsigned int)pValue;
            return;
        case VAR_PrisonTerms:
            pParty->uNumPrisonTerms -= (int)pValue;
            return;
        case VAR_ArenaWinsPage:
            pParty->uNumArenaWins[0] -= (char)pValue;
            return;
        case VAR_ArenaWinsSquire:
            pParty->uNumArenaWins[1] -= (char)pValue;
            return;
        case VAR_ArenaWinsKnight:
            pParty->uNumArenaWins[2] -= (char)pValue;
            return;
        case VAR_ArenaWinsLord:
            pParty->uNumArenaWins[4] -= (char)pValue;
            return;
        default:
            return;
    }
}

//----- (new function) --------------------------------------------------------
void Character::PlayAwardSound_Anim98() {
    int playerIndex = getCharacterIndex();
    spell_fx_renderer->SetPlayerBuffAnim(SPELL_152, playerIndex);
    PlayAwardSound();
}

//----- (new function) --------------------------------------------------------
void Character::PlayAwardSound_Anim98_Face(CharacterSpeech speech) {
    this->playReaction(speech);
    PlayAwardSound_Anim98();
}

//----- (new function) --------------------------------------------------------
void Character::SubtractSkillByEvent(CharacterSkillType skill, uint16_t subSkillValue) {
    auto [subLevel, subMastery] = CombinedSkillValue::fromJoinedUnchecked(subSkillValue);

    if (pActiveSkills[skill] == CombinedSkillValue::none())
        return; // Already at zero!

    int newLevel = std::max(1, pActiveSkills[skill].level() - subLevel);
    pActiveSkills[skill] = CombinedSkillValue(newLevel, pActiveSkills[skill].mastery());
    // TODO(pskelton): check - should this be able to forget a skill '0' or min of '1'
    // TODO(pskelton): check - should this modify mastery as well
}

//----- (00467E7F) --------------------------------------------------------
void Character::EquipBody(ITEM_EQUIP_TYPE uEquipType) {
    ITEM_SLOT itemAnchor;          // ebx@1
    int itemInvLocation;     // edx@1
    int freeSlot;            // eax@3
    ItemGen tempPickedItem;  // [sp+Ch] [bp-30h]@1

    tempPickedItem.Reset();
    itemAnchor = pEquipTypeToBodyAnchor[uEquipType];
    itemInvLocation = pParty->activeCharacter().pEquipment.pIndices[itemAnchor];
    if (itemInvLocation) {  //переодеться в другую вещь
        tempPickedItem = pParty->pPickedItem;
        pParty->activeCharacter().pInventoryItemList[itemInvLocation - 1].uBodyAnchor = ITEM_SLOT_INVALID;
        pParty->pPickedItem.Reset();
        pParty->setHoldingItem(&pParty->activeCharacter().pInventoryItemList[itemInvLocation - 1]);
        tempPickedItem.uBodyAnchor = itemAnchor;
        pParty->activeCharacter().pInventoryItemList[itemInvLocation - 1] = tempPickedItem;
        pParty->activeCharacter().pEquipment.pIndices[itemAnchor] = itemInvLocation;
    } else {  // одеть вещь
        freeSlot = pParty->activeCharacter().findFreeInventoryListSlot();
        if (freeSlot >= 0) {
            pParty->pPickedItem.uBodyAnchor = itemAnchor;
            pParty->activeCharacter().pInventoryItemList[freeSlot] = pParty->pPickedItem;
            pParty->activeCharacter().pEquipment.pIndices[itemAnchor] = freeSlot + 1;
            mouse->RemoveHoldingItem();
        }
    }
}

int cycleCharacter(bool backwards) {
    int currentId = pParty->activeCharacterIndex() - 1;

    for (int i = 0; i < pParty->pCharacters.size(); i++) {
        currentId += (backwards ? -1 : 1);

        if (currentId < 0) {
            currentId = pParty->pCharacters.size() - 1;
        } else if (currentId >= pParty->pCharacters.size()) {
            currentId = 0;
        }
        if (pParty->pCharacters[currentId].timeToRecovery == 0) {
            return currentId + 1;
        }
    }

    return pParty->activeCharacterIndex();
}

bool Character::hasUnderwaterSuitEquipped() {
    // the original function took the
    // character number as a parameter. if it
    // was 0, the whole party was checked.
    // calls with the parameter 0 have been
    // changed to calls to this for every
    // character
    if (GetArmorItem() == nullptr || GetArmorItem()->uItemID != ITEM_QUEST_WETSUIT) {
        return false;
    }
    return true;
}

bool Character::hasItem(ITEM_TYPE uItemID, bool checkHeldItem) {
    if (!checkHeldItem || pParty->pPickedItem.uItemID != uItemID) {
        for (uint i = 0; i < INVENTORY_SLOT_COUNT; ++i) {
            if (this->pInventoryMatrix[i] > 0) {
                if (this
                        ->pInventoryItemList[this->pInventoryMatrix[i] - 1]
                        .uItemID == uItemID)
                    return true;
            }
        }
        for (ITEM_SLOT i : allItemSlots()) {
            if (this->pEquipment.pIndices[i]) {
                if (this
                        ->pInventoryItemList[this->pEquipment.pIndices[i] - 1]
                        .uItemID == uItemID)
                    return true;
            }
        }
        return false;
    } else {
        return true;
    }
}
//----- (0043EDB9) --------------------------------------------------------
bool ShouldLoadTexturesForRaceAndGender(unsigned int _this) {
    CharacterRace race;  // edi@2
    CharacterSex sex;       // eax@2

    for (Character &character : pParty->pCharacters) {
        race = character.GetRace();
        sex = character.GetSexByVoice();
        switch (_this) {
            case 0:
                if ((race == CHARACTER_RACE_HUMAN ||
                     race == CHARACTER_RACE_ELF ||
                     race == CHARACTER_RACE_GOBLIN) &&
                    sex == SEX_MALE)
                    return true;
                break;
            case 1:
                if ((race == CHARACTER_RACE_HUMAN ||
                     race == CHARACTER_RACE_ELF ||
                     race == CHARACTER_RACE_GOBLIN) &&
                    sex == SEX_FEMALE)
                    return true;
                break;
            case 2:
                if (race == CHARACTER_RACE_DWARF && sex == SEX_MALE)
                    return true;
                break;
            case 3:
                if (race == CHARACTER_RACE_DWARF && sex == SEX_FEMALE)
                    return true;
                break;
        }
    }
    return false;
}

//----- (0043ED6F) --------------------------------------------------------
bool IsDwarfPresentInParty(bool a1) {
    for (Character &character : pParty->pCharacters) {
        CharacterRace race = character.GetRace();

        if (race == CHARACTER_RACE_DWARF && a1)
            return true;
        else if (race != CHARACTER_RACE_DWARF && !a1)
            return true;
    }
    return false;
}

//----- (00439FCB) --------------------------------------------------------
void DamageCharacterFromMonster(unsigned int uObjID, ABILITY_INDEX dmgSource, Vec3i *pPos, signed int targetchar) {
    // target character? if any

    SPELL_TYPE spellId;
    signed int recvdMagicDmg;     // eax@139
    int healthBeforeRecvdDamage;  // [sp+48h] [bp-Ch]@3

    ObjectType pidtype = PID_TYPE(uObjID);

    /*    OBJECT_None = 0x0,
    OBJECT_Door = 0x1,
    OBJECT_Item = 0x2,
    OBJECT_Actor = 0x3,
    OBJECT_Character = 0x4,
    OBJECT_Decoration = 0x5,
    OBJECT_Face = 0x6,*/

    if (pidtype != OBJECT_Item) {  // not an item
        // hit by monster
        if (pidtype != OBJECT_Actor) __debugbreak();

        if (targetchar == -1) __debugbreak();

        unsigned int uActorID = PID_ID(uObjID);

        Character *playerPtr = &pParty->pCharacters[targetchar];
        Actor *actorPtr = &pActors[uActorID];
        healthBeforeRecvdDamage = playerPtr->health;
        if (PID_TYPE(uObjID) != OBJECT_Actor || !actorPtr->ActorHitOrMiss(playerPtr))
            return;

        // GM unarmed 1% chance to evade attacks per skill point
        if (playerPtr->getActualSkillValue(CHARACTER_SKILL_UNARMED).mastery() >= CHARACTER_SKILL_MASTERY_GRANDMASTER &&
            grng->random(100) < playerPtr->getActualSkillValue(CHARACTER_SKILL_UNARMED).level()) {
            GameUI_SetStatusBar(LSTR_FMT_S_EVADES_DAMAGE, playerPtr->name);
            playerPtr->playReaction(SPEECH_AVOID_DAMAGE);
            return;
        }

        // play hit sound
        ItemGen *equippedArmor = playerPtr->GetArmorItem();
        SoundID soundToPlay;
        if (!equippedArmor || equippedArmor->IsBroken() ||
            (equippedArmor->GetPlayerSkillType() != CHARACTER_SKILL_CHAIN &&
             equippedArmor->GetPlayerSkillType() != CHARACTER_SKILL_PLATE)) {
            int randVal = vrng->random(4);
            switch (randVal) {
                case 0:
                    soundToPlay = SOUND_dull_armor_strike1;
                    break;
                case 1:
                    soundToPlay = SOUND_dull_armor_strike2;
                    break;
                case 2:
                    soundToPlay = SOUND_dull_armor_strike3;
                    break;
                case 3:
                    soundToPlay = SOUND_dull_strike;
                    break;
                default:
                    Error("Unexpected sound value");
            }
        } else {
            int randVal = vrng->random(4);
            switch (randVal) {
                case 0:
                    soundToPlay = SOUND_metal_armor_strike1;
                    break;
                case 1:
                    soundToPlay = SOUND_metal_armor_strike2;
                    break;
                case 2:
                    soundToPlay = SOUND_metal_armor_strike3;
                    break;
                case 3:
                    soundToPlay = SOUND_metal_vs_metal01h;
                    break;
                default:
                    Error("Unexpected sound value");
            }
        }
        // TODO(Nik-RE-dev): is it correct to use voice volume for strike sounds?
        pAudioPlayer->playSound(soundToPlay, PID(OBJECT_Character, targetchar));

        // calc damage
        int dmgToReceive = actorPtr->_43B3E0_CalcDamage(dmgSource);
        if (actorPtr->buffs[ACTOR_BUFF_SHRINK].Active()) {
            int16_t spellPower = actorPtr->buffs[ACTOR_BUFF_SHRINK].power;
            if (spellPower > 0)
                dmgToReceive /= spellPower;
        }

        int damageType;
        switch (dmgSource) {
            case ABILITY_ATTACK1:
                damageType = actorPtr->monsterInfo.uAttack1Type;
                break;
            case ABILITY_ATTACK2:
                damageType = actorPtr->monsterInfo.uAttack2Type;
                break;
            case ABILITY_SPELL1:
                spellId = actorPtr->monsterInfo.uSpell1ID;
                damageType = pSpellStats->pInfos[spellId].uSchool;
                break;
            case ABILITY_SPELL2:
                spellId = actorPtr->monsterInfo.uSpell2ID;
                damageType = pSpellStats->pInfos[spellId].uSchool;
                break;
            case ABILITY_SPECIAL:
                damageType = actorPtr->monsterInfo.field_3C_some_special_attack;
                break;
            default:
                damageType = 4;  // DMGT_PHISYCAL
                break;
        }

        // calc damage
        dmgToReceive = playerPtr->receiveDamage(dmgToReceive, (DAMAGE_TYPE)damageType);

        // pain reflection back on attacker
        if (playerPtr->pCharacterBuffs[CHARACTER_BUFF_PAIN_REFLECTION].Active()) {
            AIState actorState = actorPtr->aiState;
            if (actorState != Dying && actorState != Dead) {
                int reflectedDamage = actorPtr->CalcMagicalDamageToActor((DAMAGE_TYPE)damageType, dmgToReceive);
                actorPtr->currentHP -= reflectedDamage;
                if (reflectedDamage >= 0) {
                    if (actorPtr->currentHP >= 1) {
                        Actor::AI_Stun(uActorID, PID(OBJECT_Character, targetchar), 0);  // todo extract this branch to a function
                                    // once Actor::functions are changed to
                                    // nonstatic actor functions
                        Actor::AggroSurroundingPeasants(uActorID, 1);
                    } else {
                        // actor has died from retaliation
                        Actor::Die(uActorID);
                        Actor::ApplyFineForKillingPeasant(uActorID);
                        Actor::AggroSurroundingPeasants(uActorID, 1);
                        if (actorPtr->monsterInfo.uExp)
                            pParty->GivePartyExp(pMonsterStats->pInfos[actorPtr->monsterInfo.uID].uExp);

                        // kill speech
                        CharacterSpeech speechToPlay = SPEECH_ATTACK_HIT;
                        if (vrng->random(100) < 20) {
                            speechToPlay = actorPtr->monsterInfo.uHP >= 100 ? SPEECH_KILL_STRONG_ENEMY : SPEECH_KILL_WEAK_ENEMY;
                        }
                        playerPtr->playReaction(speechToPlay);
                    }
                }
            }
        }

        // special attack trigger
        if (!engine->config->debug.NoDamage.value() && actorPtr->monsterInfo.uSpecialAttackType && grng->random(100) < actorPtr->monsterInfo.uLevel *
                                                                                                                       actorPtr->monsterInfo.uSpecialAttackLevel) {
            playerPtr->ReceiveSpecialAttackEffect(actorPtr->monsterInfo.uSpecialAttackType, actorPtr);
        }

        // add recovery after being hit
        if (!pParty->bTurnBasedModeOn) {
            int actEndurance = playerPtr->GetActualEndurance();
            int recoveryTime = (int)((20 - playerPtr->GetParameterBonus(actEndurance)) *
                      debug_non_combat_recovery_mul * flt_debugrecmod3);
            playerPtr->SetRecoveryTime(recoveryTime);
        }

        // badly hurt speech
        int yellThreshold = playerPtr->GetMaxHealth() / 4;
        if (yellThreshold > playerPtr->health &&
            yellThreshold <= healthBeforeRecvdDamage &&
            playerPtr->health > 0) {
            playerPtr->playReaction(SPEECH_BADLY_HURT);
        }
        return;
    } else {  // is an item
        int spriteId = PID_ID(uObjID);
        SpriteObject *spritefrom = &pSpriteObjects[spriteId];
        ObjectType uActorType = PID_TYPE(spritefrom->spell_caster_pid);
        int uActorID = PID_ID(spritefrom->spell_caster_pid);

        if (uActorType == OBJECT_Item) {
            Character *playerPtr;  // eax@81

            // select char target or pick random
            if (targetchar != -1) {
                playerPtr = &pParty->pCharacters[targetchar];
            } else {
                int id = pParty->getRandomActiveCharacterId(grng.get());

                if (id != -1) {
                    playerPtr = &pParty->pCharacters[id];
                } else {
                    // for rare instances where party is "dead" at this point but still being damaged
                    playerPtr = &pParty->pCharacters[grng->random(3)];
                }
            }

            int damage;
            int damagetype;
            if (uActorType != OBJECT_Character ||spritefrom->uSpellID != SPELL_BOW_ARROW) {
                int playerMaxHp = playerPtr->GetMaxHealth();
                damage = CalcSpellDamage(spritefrom->uSpellID,
                                         spritefrom->spell_level,
                                         spritefrom->spell_skill, playerMaxHp);
                damagetype = pSpellStats->pInfos[spritefrom->uSpellID].uSchool;
            } else {
                damage = pParty->pCharacters[uActorID].CalculateRangedDamageTo(0);
                damagetype = 0;
            }
            playerPtr->receiveDamage(damage, (DAMAGE_TYPE)damagetype);
            if (uActorType == OBJECT_Character) {
                pParty->setDelayedReaction(SPEECH_DAMAGED_PARTY, uActorID);
            }
            return;
        } else if (uActorType == OBJECT_Actor) {  // missile fired by actor
            Actor *actorPtr = &pActors[uActorID];
            if (targetchar == -1) targetchar = stru_50C198.which_player_to_attack(actorPtr);
            Character *playerPtr = &pParty->pCharacters[targetchar];
            int dmgToReceive = actorPtr->_43B3E0_CalcDamage(dmgSource);
            uint16_t spriteType = spritefrom->uType;

            if (spritefrom->uType == SPRITE_ARROW_PROJECTILE) {  // arrows
                // GM unarmed 1% chance to evade attack per skill point
                if (playerPtr->getActualSkillValue(CHARACTER_SKILL_UNARMED).mastery() >= CHARACTER_SKILL_MASTERY_GRANDMASTER &&
                    grng->random(100) < playerPtr->getActualSkillValue(CHARACTER_SKILL_UNARMED).level()) {
                    GameUI_SetStatusBar(LSTR_FMT_S_EVADES_DAMAGE, playerPtr->name);
                    playerPtr->playReaction(SPEECH_AVOID_DAMAGE);
                    return;
                }
            } else if (spriteType == SPRITE_BLASTER_PROJECTILE ||
                       spriteType == SPRITE_PROJECTILE_AIRBOLT ||  // dragonflies firebolt
                       spriteType == SPRITE_PROJECTILE_EARTHBOLT ||
                       spriteType == SPRITE_PROJECTILE_FIREBOLT ||
                       spriteType == SPRITE_PROJECTILE_WATERBOLT ||
                       spriteType == SPRITE_PROJECTILE_520 ||
                       spriteType == SPRITE_PROJECTILE_525 ||
                       spriteType == SPRITE_PROJECTILE_530 ||
                       spriteType == SPRITE_PROJECTILE_LIGHTBOLT ||
                       spriteType == SPRITE_PROJECTILE_DARKBOLT) {
                // reduce missle damage with skills / armour
                if (!actorPtr->ActorHitOrMiss(playerPtr)) return;
                if (playerPtr->pCharacterBuffs[CHARACTER_BUFF_SHIELD].Active()) dmgToReceive >>= 1;
                if (playerPtr->HasEnchantedItemEquipped(ITEM_ENCHANTMENT_OF_SHIELDING)) dmgToReceive >>= 1;
                if (playerPtr->HasEnchantedItemEquipped(ITEM_ENCHANTMENT_OF_STORM)) dmgToReceive >>= 1;
                if (playerPtr->HasItemEquipped(ITEM_SLOT_ARMOUR) &&
                    playerPtr->GetArmorItem()->uItemID == ITEM_ARTIFACT_GOVERNORS_ARMOR)
                    dmgToReceive >>= 1;
                if (playerPtr->HasItemEquipped(ITEM_SLOT_MAIN_HAND)) {
                    ItemGen *mainHandItem = playerPtr->GetMainHandItem();
                    if (mainHandItem->uItemID == ITEM_RELIC_KELEBRIM ||
                        mainHandItem->uItemID == ITEM_ARTIFACT_ELFBANE ||
                        (mainHandItem->isShield() && playerPtr->getActualSkillValue(CHARACTER_SKILL_SHIELD).mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER))
                        dmgToReceive >>= 1;
                }
                if (playerPtr->HasItemEquipped(ITEM_SLOT_OFF_HAND)) {
                    ItemGen *offHandItem = playerPtr->GetOffHandItem();
                    if (offHandItem->uItemID == ITEM_RELIC_KELEBRIM ||
                        offHandItem->uItemID == ITEM_ARTIFACT_ELFBANE ||
                        (offHandItem->isShield() && playerPtr->getActualSkillValue(CHARACTER_SKILL_SHIELD).mastery() == CHARACTER_SKILL_MASTERY_GRANDMASTER))
                        dmgToReceive >>= 1;
                }
            }

            if (actorPtr->buffs[ACTOR_BUFF_SHRINK].Active()) {
                int spellPower = actorPtr->buffs[ACTOR_BUFF_SHRINK].power;
                if (spellPower > 0) dmgToReceive /= spellPower;
            }

            int damageType;
            switch (dmgSource) {
                case ABILITY_ATTACK1:
                    damageType = actorPtr->monsterInfo.uAttack1Type;
                    break;
                case ABILITY_ATTACK2:
                    damageType = actorPtr->monsterInfo.uAttack2Type;
                    break;
                case ABILITY_SPELL1:
                    spellId = actorPtr->monsterInfo.uSpell1ID;
                    damageType = pSpellStats->pInfos[spellId].uSchool;
                    break;
                case ABILITY_SPELL2:
                    spellId = actorPtr->monsterInfo.uSpell2ID;
                    damageType = pSpellStats->pInfos[spellId].uSchool;
                    break;
                case ABILITY_SPECIAL:
                    damageType = actorPtr->monsterInfo.field_3C_some_special_attack;
                    break;
                default:
                    damageType = 4;
                    break;
            }

            int reflectedDmg = playerPtr->receiveDamage(dmgToReceive, (DAMAGE_TYPE)damageType);
            if (playerPtr->pCharacterBuffs[CHARACTER_BUFF_PAIN_REFLECTION].Active()) {
                AIState actorState = actorPtr->aiState;
                if (actorState != Dying && actorState != Dead) {
                    recvdMagicDmg = actorPtr->CalcMagicalDamageToActor((DAMAGE_TYPE)damageType, reflectedDmg);
                    actorPtr->currentHP -= recvdMagicDmg;

                    if (recvdMagicDmg >= 0) {
                        if (actorPtr->currentHP >= 1) {
                            Actor::AI_Stun(uActorID, PID(OBJECT_Character, targetchar), 0);
                            Actor::AggroSurroundingPeasants(uActorID, 1);
                        } else {
                            // actor killed by retaliation
                            Actor::Die(uActorID);
                            Actor::ApplyFineForKillingPeasant(uActorID);
                            Actor::AggroSurroundingPeasants(uActorID, 1);
                            if (actorPtr->monsterInfo.uExp)
                                pParty->GivePartyExp(pMonsterStats->pInfos[actorPtr->monsterInfo.uID].uExp);

                            CharacterSpeech speechToPlay = SPEECH_ATTACK_HIT;
                            if (vrng->random(100) < 20) {
                                speechToPlay = actorPtr->monsterInfo.uHP >= 100 ? SPEECH_KILL_STRONG_ENEMY : SPEECH_KILL_WEAK_ENEMY;
                            }
                            playerPtr->playReaction(speechToPlay);
                        }
                    }
                }
            }

            // special attack trigger
            if (dmgSource == ABILITY_ATTACK1 && !engine->config->debug.NoDamage.value() &&
                actorPtr->monsterInfo.uSpecialAttackType && grng->random(100) < actorPtr->monsterInfo.uLevel *
                                                                                actorPtr->monsterInfo.uSpecialAttackLevel) {
                playerPtr->ReceiveSpecialAttackEffect(actorPtr->monsterInfo.uSpecialAttackType, actorPtr);
            }

            // set recovery after hit
            if (!pParty->bTurnBasedModeOn) {
                int actEnd = playerPtr->GetActualEndurance();
                int recTime =
                    (int)((20 - playerPtr->GetParameterBonus(actEnd)) *
                          debug_non_combat_recovery_mul * flt_debugrecmod3);
                playerPtr->SetRecoveryTime(recTime);
            }
            return;
        } else {
            // party hits self
            Character *playerPtr = &pParty->pCharacters[targetchar];
            int damage;
            int damagetype;
            if (uActorType != OBJECT_Character ||
                spritefrom->uSpellID != SPELL_BOW_ARROW) {
                int playerMaxHp = playerPtr->GetMaxHealth();
                damage = CalcSpellDamage(spritefrom->uSpellID,
                                         spritefrom->spell_level,
                                         spritefrom->spell_skill, playerMaxHp);
                damagetype = pSpellStats->pInfos[spritefrom->uSpellID].uSchool;
            } else {
                damage = pParty->pCharacters[uActorID].CalculateRangedDamageTo(0);
                damagetype = 0;
            }

            playerPtr->receiveDamage(damage, (DAMAGE_TYPE)damagetype);
            if (uActorType == OBJECT_Character) {
                pParty->setDelayedReaction(SPEECH_DAMAGED_PARTY, uActorID);
            }

            return;
        }
    }
}

void Character::OnInventoryLeftClick() {
    ITEM_TYPE pickedItemId;  // esi@12
    unsigned int invItemIndex;  // eax@12
    unsigned int itemPos;       // eax@18
    ItemGen tmpItem;            // [sp+Ch] [bp-3Ch]@1

    CastSpellInfo *pSpellInfo;

    if (current_character_screen_window == WINDOW_CharacterWindow_Inventory) {
        int pY;
        int pX;
        mouse->GetClickPos(&pX, &pY);

        int inventoryYCoord = (pY - 17) / 32;
        int inventoryXCoord = (pX - 14) / 32;
        int invMatrixIndex = inventoryXCoord + (INVENTORY_SLOTS_WIDTH * inventoryYCoord);

        if (inventoryYCoord >= 0 && inventoryYCoord < INVENTORY_SLOTS_HEIGHT &&
            inventoryXCoord >= 0 && inventoryXCoord < INVENTORY_SLOTS_WIDTH) {
            if (IsEnchantingInProgress) {
                unsigned int enchantedItemPos = this->GetItemListAtInventoryIndex(invMatrixIndex);

                if (enchantedItemPos) {
                    /* *((char *)pGUIWindow_CastTargetedSpell->ptr_1C + 8) &=
                     *0x7Fu;
                     *((short *)pGUIWindow_CastTargetedSpell->ptr_1C + 2) =
                     *pParty->activeCharacterIndex() - 1;
                     *((int *)pGUIWindow_CastTargetedSpell->ptr_1C + 3) =
                     *enchantedItemPos - 1;
                     *((short *)pGUIWindow_CastTargetedSpell->ptr_1C + 3) =
                     *invMatrixIndex;*/
                    pSpellInfo = static_cast<CastSpellInfo *>(pGUIWindow_CastTargetedSpell->wData.ptr);
                    pSpellInfo->uFlags &= ~ON_CAST_TargetedEnchantment;
                    pSpellInfo->uPlayerID_2 = pParty->activeCharacterIndex() - 1;
                    pSpellInfo->spell_target_pid = enchantedItemPos - 1;
                    pSpellInfo->field_6 = this->GetItemMainInventoryIndex(invMatrixIndex);
                    ptr_50C9A4_ItemToEnchant = &this->pInventoryItemList[enchantedItemPos - 1];
                    IsEnchantingInProgress = false;

                    engine->_messageQueue->clear();

                    mouse->SetCursorImage("MICON1");
                    AfterEnchClickEventId = UIMSG_Escape;
                    AfterEnchClickEventSecondParam = 0;
                    AfterEnchClickEventTimeout = Timer::Second * 2;
                }

                return;
            }

            if (ptr_50C9A4_ItemToEnchant)
                return;

            pickedItemId = pParty->pPickedItem.uItemID;
            invItemIndex = this->GetItemListAtInventoryIndex(invMatrixIndex);

            if (pickedItemId == ITEM_NULL) {  // no hold item
                if (!invItemIndex) {
                    return;
                } else {
                    memcpy(&pParty->pPickedItem, &this->pInventoryItemList[invItemIndex - 1], sizeof(pParty->pPickedItem));
                    this->RemoveItemAtInventoryIndex(invMatrixIndex);
                    pickedItemId = pParty->pPickedItem.uItemID;
                    mouse->SetCursorImage(pItemTable->pItems[pickedItemId].iconName);
                    return;
                }
            } else {  // hold item
                if (invItemIndex) {
                    ItemGen *invItemPtr = &this->pInventoryItemList[invItemIndex - 1];
                    memcpy(&tmpItem, invItemPtr, sizeof(tmpItem));
                    int oldinvMatrixIndex = invMatrixIndex;
                    invMatrixIndex = GetItemMainInventoryIndex(invMatrixIndex);
                    this->RemoveItemAtInventoryIndex(oldinvMatrixIndex);
                    int emptyIndex = this->AddItem2(invMatrixIndex, &pParty->pPickedItem);

                    if (!emptyIndex) {
                        emptyIndex = this->AddItem2(-1, &pParty->pPickedItem);
                        if (!emptyIndex) {
                            this->PutItemArInventoryIndex(tmpItem.uItemID, invItemIndex - 1, invMatrixIndex);
                            memcpy(invItemPtr, &tmpItem, sizeof(ItemGen));
                            return;
                        }
                    }

                    memcpy(&pParty->pPickedItem, &tmpItem, sizeof(ItemGen));
                    mouse->SetCursorImage(pParty->pPickedItem.GetIconName());
                    return;
                } else {
                    itemPos = this->AddItem(invMatrixIndex, pickedItemId);

                    if (itemPos) {
                        memcpy(&this->pInventoryItemList[itemPos - 1], &pParty->pPickedItem, sizeof(ItemGen));
                        mouse->RemoveHoldingItem();
                        return;
                    }

                    // itemPos = this->AddItem(-1, pickedItemId);

                    // if ( itemPos ) {
                    //    memcpy(&this->pInventoryItemList[itemPos-1],
                    // &pParty->pPickedItem, sizeof(ItemGen));
                    //    pMouse->RemoveHoldingItem();
                    //       return;
                    // }
                }
            }  // held item or no
        }  // limits
    }      // char wind
}  // func

bool Character::IsWeak() const {
    return this->conditions.Has(CONDITION_WEAK);
}

bool Character::IsDead() const {
    return this->conditions.Has(CONDITION_DEAD);
}

bool Character::IsEradicated() const {
    return this->conditions.Has(CONDITION_ERADICATED);
}

bool Character::IsZombie() const {
    return this->conditions.Has(CONDITION_ZOMBIE);
}

bool Character::IsCursed() const {
    return this->conditions.Has(CONDITION_CURSED);
}

bool Character::IsPertified() const {
    return this->conditions.Has(CONDITION_PETRIFIED);
}

bool Character::IsUnconcious() const {
    return this->conditions.Has(CONDITION_UNCONSCIOUS);
}

bool Character::IsAsleep() const {
    return this->conditions.Has(CONDITION_SLEEP);
}

bool Character::IsParalyzed() const {
    return this->conditions.Has(CONDITION_PARALYZED);
}

bool Character::IsDrunk() const {
    return this->conditions.Has(CONDITION_DRUNK);
}

void Character::SetCursed(GameTime time) {
    this->conditions.Set(CONDITION_CURSED, time);
}

void Character::SetWeak(GameTime time) {
    this->conditions.Set(CONDITION_WEAK, time);
}

void Character::SetAsleep(GameTime time) {
    this->conditions.Set(CONDITION_SLEEP, time);
}

void Character::SetAfraid(GameTime time) {
    this->conditions.Set(CONDITION_FEAR, time);
}

void Character::SetDrunk(GameTime time) {
    this->conditions.Set(CONDITION_DRUNK, time);
}

void Character::SetInsane(GameTime time) {
    this->conditions.Set(CONDITION_INSANE, time);
}

void Character::SetPoisonWeak(GameTime time) {
    this->conditions.Set(CONDITION_POISON_WEAK, time);
}

void Character::SetDiseaseWeak(GameTime time) {
    this->conditions.Set(CONDITION_DISEASE_WEAK, time);
}

void Character::SetPoisonMedium(GameTime time) {
    this->conditions.Set(CONDITION_POISON_MEDIUM, time);
}

void Character::SetDiseaseMedium(GameTime time) {
    this->conditions.Set(CONDITION_DISEASE_MEDIUM, time);
}

void Character::SetPoisonSevere(GameTime time) {
    this->conditions.Set(CONDITION_POISON_SEVERE, time);
}

void Character::SetDiseaseSevere(GameTime time) {
    this->conditions.Set(CONDITION_DISEASE_SEVERE, time);
}

void Character::SetParalyzed(GameTime time) {
    this->conditions.Set(CONDITION_PARALYZED, time);
}

void Character::SetUnconcious(GameTime time) {
    this->conditions.Set(CONDITION_UNCONSCIOUS, time);
}

void Character::SetDead(GameTime time) {
    this->conditions.Set(CONDITION_DEAD, time);
}

void Character::SetPertified(GameTime time) {
    this->conditions.Set(CONDITION_PETRIFIED, time);
}

void Character::SetEradicated(GameTime time) {
    this->conditions.Set(CONDITION_ERADICATED, time);
}

void Character::SetZombie(GameTime time) {
    this->conditions.Set(CONDITION_ZOMBIE, time);
}

void Character::SetCondWeakWithBlockCheck(int blockable) {
    SetCondition(CONDITION_WEAK, blockable);
}

void Character::SetCondInsaneWithBlockCheck(int blockable) {
    SetCondition(CONDITION_INSANE, blockable);
}

void Character::SetCondDeadWithBlockCheck(int blockable) {
    SetCondition(CONDITION_DEAD, blockable);
}

void Character::SetCondUnconsciousWithBlockCheck(int blockable) {
    SetCondition(CONDITION_UNCONSCIOUS, blockable);
}

ItemGen *Character::GetOffHandItem() { return GetItem(&CharacterEquipment::uOffHand); }
const ItemGen *Character::GetOffHandItem() const { return GetItem(&CharacterEquipment::uOffHand); }

ItemGen *Character::GetMainHandItem() { return GetItem(&CharacterEquipment::uMainHand); }
const ItemGen *Character::GetMainHandItem() const { return GetItem(&CharacterEquipment::uMainHand); }

ItemGen *Character::GetBowItem() { return GetItem(&CharacterEquipment::uBow); }
const ItemGen *Character::GetBowItem() const { return GetItem(&CharacterEquipment::uBow); }

ItemGen *Character::GetArmorItem() { return GetItem(&CharacterEquipment::uArmor); }
const ItemGen *Character::GetArmorItem() const { return GetItem(&CharacterEquipment::uArmor); }

ItemGen *Character::GetHelmItem() { return GetItem(&CharacterEquipment::uHelm); }
const ItemGen *Character::GetHelmItem() const { return GetItem(&CharacterEquipment::uHelm); }

ItemGen *Character::GetBeltItem() { return GetItem(&CharacterEquipment::uBelt); }
const ItemGen *Character::GetBeltItem() const { return GetItem(&CharacterEquipment::uBelt); }

ItemGen *Character::GetCloakItem() { return GetItem(&CharacterEquipment::uCloak); }
const ItemGen *Character::GetCloakItem() const { return GetItem(&CharacterEquipment::uCloak); }

ItemGen *Character::GetGloveItem() { return GetItem(&CharacterEquipment::uGlove); }
const ItemGen *Character::GetGloveItem() const { return GetItem(&CharacterEquipment::uGlove); }

ItemGen *Character::GetBootItem() { return GetItem(&CharacterEquipment::uBoot); }
const ItemGen *Character::GetBootItem() const { return GetItem(&CharacterEquipment::uBoot); }

ItemGen *Character::GetAmuletItem() { return GetItem(&CharacterEquipment::uAmulet); }
const ItemGen *Character::GetAmuletItem() const { return GetItem(&CharacterEquipment::uAmulet); }

ItemGen *Character::GetNthRingItem(int ringNum) {
    return GetNthEquippedIndexItem(ringSlot(ringNum));
}
const ItemGen *Character::GetNthRingItem(int ringNum) const { return GetNthEquippedIndexItem(ringSlot(ringNum)); }

ItemGen *Character::GetNthEquippedIndexItem(ITEM_SLOT index) {
    if (this->pEquipment.pIndices[index] == 0) {
        return nullptr;
    }

    return &this->pInventoryItemList[this->pEquipment.pIndices[index] - 1];
}

const ItemGen *Character::GetNthEquippedIndexItem(ITEM_SLOT index) const {
    return const_cast<Character *>(this)->GetNthEquippedIndexItem(index);
}

ItemGen *Character::GetItem(unsigned int CharacterEquipment::*itemPos) {
    if (this->pEquipment.*itemPos == 0) {
        return nullptr;
    }

    return &this->pInventoryItemList[this->pEquipment.*itemPos - 1];
}
const ItemGen *Character::GetItem(unsigned int CharacterEquipment::*itemPos) const {
    return const_cast<Character *>(this)->GetItem(itemPos);
}

int Character::getCharacterIndex() {
    return pParty->getCharacterIdInParty(this);
}

//----- (004272F5) --------------------------------------------------------
bool Character::characterHitOrMiss(Actor *pActor, int distancemod, int skillmod) {  // PS - RETURN IF ATTACK WILL HIT
    int naturalArmor = pActor->monsterInfo.uAC;  // actor usual armour
    int armorBuff = 0;

    if (pActor->buffs[ACTOR_BUFF_SOMETHING_THAT_HALVES_AC]
            .Active())  // gm axe effect??
        naturalArmor /= 2;

    if (pActor->buffs[ACTOR_BUFF_HOUR_OF_POWER].Active())
        armorBuff = pActor->buffs[ACTOR_BUFF_SHIELD].power;

    if (pActor->buffs[ACTOR_BUFF_STONESKIN].Active() &&
        pActor->buffs[ACTOR_BUFF_STONESKIN].power > armorBuff)
        armorBuff = pActor->buffs[ACTOR_BUFF_STONESKIN].power;

    int effectiveActorArmor = armorBuff + naturalArmor;

    int attBonus;  // character attack bonus
    if (distancemod)
        attBonus = this->GetRangedAttack();  // range
    else
        attBonus = this->GetActualAttack(false);  // melee

    int attPositiveMod =
        skillmod + grng->random(effectiveActorArmor + 2 * attBonus + 30);  // positive effects to hit on attack

    int attNegativeMod;  // negative effects to hit on attack

    if (distancemod == 2) {  // medium range
        attNegativeMod =
            ((effectiveActorArmor + 15) / 2) + effectiveActorArmor + 15;
    } else if (distancemod == 3) {  // far range
        attNegativeMod = 2 * effectiveActorArmor + 30;
    } else {  // close range
        attNegativeMod = effectiveActorArmor + 15;
    }

    return (attPositiveMod > attNegativeMod);  // do the pos outweight the negs
}

//----- (0042ECB5) --------------------------------------------------------
void Character::_42ECB5_CharacterAttacksActor() {
    //  char *v5; // eax@8
    //  unsigned int v9; // ecx@21
    //  char *v11; // eax@26
    //  unsigned int v12; // eax@47
    //  SoundID v24; // [sp-4h] [bp-40h]@58

    // result = pParty->activeCharacter().CanAct();
    Character *character = &pParty->activeCharacter();
    if (!character->CanAct()) return;

    CastSpellInfoHelpers::cancelSpellCastInProgress();
    // v3 = 0;
    if (pParty->Invisible())
        pParty->pPartyBuffs[PARTY_BUFF_INVISIBILITY].Reset();

    // v31 = character->pEquipment.uBow;
    int bow_idx = character->pEquipment.uBow;
    if (bow_idx && character->pInventoryItemList[bow_idx - 1].IsBroken())
        bow_idx = 0;

    // v32 = 0;
    ITEM_TYPE wand_item_id = ITEM_NULL;
    // v33 = 0;

    ITEM_TYPE laser_weapon_item_id = ITEM_NULL;

    int main_hand_idx = character->pEquipment.uMainHand;
    if (main_hand_idx) {
        const ItemGen *item = &character->pInventoryItemList[main_hand_idx - 1];
        // v5 = (char *)v1 + 36 * v4;
        if (!item->IsBroken()) {
            // v28b = &v1->pInventoryItems[v4].uItemID;
            // v6 = v1->pInventoryItems[v4].uItemID;//*((int *)v5 + 124);
            if (item->isWand()) {
                if (item->uNumCharges <= 0)
                    character->pEquipment.uMainHand =
                        0;  // wand discharged - unequip
                else
                    wand_item_id = item->uItemID;  // *((int *)v5 + 124);
            } else if (item->uItemID == ITEM_BLASTER ||
                       item->uItemID == ITEM_BLASTER_RIFLE) {
                laser_weapon_item_id = item->uItemID;  // *((int *)v5 + 124);
            }
        }
    }

    // v30 = 0;
    // v29 = 0;
    // v28 = 0;
    // v7 = pMouse->uPointingObjectID;

    int target_pid = mouse->uPointingObjectID;
    ObjectType target_type = PID_TYPE(target_pid);
    int target_id = PID_ID(target_pid);
    if (target_type != OBJECT_Actor || !pActors[target_id].CanAct()) {
        target_pid = stru_50C198.FindClosestActor(5120, 0, 0);
        target_type = PID_TYPE(target_pid);
        target_id = PID_ID(target_pid);
    }

    Actor *actor = nullptr;
    if (target_id < 500) {
        actor = &pActors[target_id];  // prevent crash
    }

    int actor_distance = 0;
    if (target_type == OBJECT_Actor) {
        int distance_x = actor->pos.x - pParty->pos.x,
            distance_y = actor->pos.y - pParty->pos.y,
            distance_z = actor->pos.z - pParty->pos.z;
        actor_distance =
            integer_sqrt(distance_x * distance_x + distance_y * distance_y +
                         distance_z * distance_z) -
            actor->radius;
        if (actor_distance < 0) actor_distance = 0;
    }

    bool shooting_bow = false, shotting_laser = false, shooting_wand = false,
         melee_attack = false;
    if (laser_weapon_item_id != ITEM_NULL) {
        shotting_laser = true;
        pushSpellOrRangedAttack(SPELL_LASER_PROJECTILE,
                                pParty->activeCharacterIndex() - 1, CombinedSkillValue::none(), 0,
                                pParty->activeCharacterIndex() + 8);
    } else if (wand_item_id != ITEM_NULL) {
        shooting_wand = true;

        int main_hand_idx = character->pEquipment.uMainHand;
        pushSpellOrRangedAttack(wandSpellIds[character->pInventoryItemList[main_hand_idx - 1].uItemID],
                                pParty->activeCharacterIndex() - 1, CombinedSkillValue::novice(8), 0, pParty->activeCharacterIndex() + 8);

        if (!--character->pInventoryItemList[main_hand_idx - 1].uNumCharges)
            character->pEquipment.uMainHand = 0;
    } else if (target_type == OBJECT_Actor && actor_distance <= 407.2) {
        melee_attack = true;

        Vec3i a3 = actor->pos - pParty->pos;
        normalize_to_fixpoint(&a3.x, &a3.y, &a3.z);

        Actor::DamageMonsterFromParty(PID(OBJECT_Character, pParty->activeCharacterIndex() - 1),
                                      target_id, &a3);
        if (character->WearsItem(ITEM_ARTIFACT_SPLITTER, ITEM_SLOT_MAIN_HAND) ||
            character->WearsItem(ITEM_ARTIFACT_SPLITTER, ITEM_SLOT_OFF_HAND))
            _42FA66_do_explosive_impact(
                actor->pos.x, actor->pos.y,
                actor->pos.z + actor->height / 2, 0, 512,
                pParty->activeCharacterIndex());
    } else if (bow_idx) {
        shooting_bow = true;
        pushSpellOrRangedAttack(SPELL_BOW_ARROW, pParty->activeCharacterIndex() - 1, CombinedSkillValue::none(), 0, 0);
    } else {
        melee_attack = true;
        // ; // actor out of range or no actor; no ranged weapon so melee
        // attacking air
    }

    if (!pParty->bTurnBasedModeOn && melee_attack) {
        // wands, bows & lasers will add recovery while shooting spell effect
        int recovery = character->GetAttackRecoveryTime(false);
        character->SetRecoveryTime(static_cast<int>(debug_non_combat_recovery_mul * recovery * flt_debugrecmod3));
    }

    CharacterSkillType skill = CHARACTER_SKILL_STAFF;
    if (shooting_wand) {
        return;
    } else if (shooting_bow) {
        skill = CHARACTER_SKILL_BOW;
        character->playReaction(SPEECH_SHOOT);
    } else if (shotting_laser) {
        skill = CHARACTER_SKILL_BLASTER;
    } else {
        int main_hand_idx = character->pEquipment.uMainHand;
        if (character->HasItemEquipped(ITEM_SLOT_MAIN_HAND) && main_hand_idx)
            skill = character->pInventoryItemList[main_hand_idx - 1].GetPlayerSkillType();

        pTurnEngine->ApplyPlayerAction();
    }

    switch (skill) {
        case CHARACTER_SKILL_STAFF:
            pAudioPlayer->playUISound(SOUND_swing_with_blunt_weapon01);
            break;
        case CHARACTER_SKILL_SWORD:
            pAudioPlayer->playUISound(SOUND_swing_with_sword01);
            break;
        case CHARACTER_SKILL_DAGGER:
            pAudioPlayer->playUISound(SOUND_swing_with_sword02);
            break;
        case CHARACTER_SKILL_AXE:
            pAudioPlayer->playUISound(SOUND_swing_with_axe01);
            break;
        case CHARACTER_SKILL_SPEAR:
            pAudioPlayer->playUISound(SOUND_swing_with_axe03);
            break;
        case CHARACTER_SKILL_BOW:
            pAudioPlayer->playUISound(SOUND_shoot_bow01);
            break;
        case CHARACTER_SKILL_MACE:
            pAudioPlayer->playUISound(SOUND_swing_with_blunt_weapon03);
            break;
        case CHARACTER_SKILL_BLASTER:
            pAudioPlayer->playUISound(SOUND_shoot_blaster01);
            break;
        default:
            break;
    }
}

//----- (0042FA66) --------------------------------------------------------
void Character::_42FA66_do_explosive_impact(int xpos, int ypos, int zpos, int a4,
                                         int16_t a5, signed int actchar) {
        // EXPLOSIVE IMPACT OF ARTIFACT SPLITTER

    // a5 is range?

    SpriteObject a1a;
    a1a.uType = SPRITE_OBJECT_EXPLODE;
    a1a.containing_item.Reset();
    a1a.uSpellID = SPELL_FIRE_FIREBALL;
    a1a.spell_level = 8;
    a1a.spell_skill = CHARACTER_SKILL_MASTERY_MASTER;
    a1a.uObjectDescID = pObjectList->ObjectIDByItemID(a1a.uType);
    a1a.vPosition.x = xpos;
    a1a.vPosition.y = ypos;
    a1a.vPosition.z = zpos;
    a1a.uAttributes = 0;
    a1a.uSectorID = pIndoor->GetSector(xpos, ypos, zpos);
    a1a.uSpriteFrameID = 0;
    a1a.spell_target_pid = 0;
    a1a.field_60_distance_related_prolly_lod = 0;
    a1a.uFacing = 0;
    a1a.uSoundID = 0;

    if (actchar >= 1 || actchar <= 4) {
        a1a.spell_caster_pid = PID(OBJECT_Character, actchar - 1);
    } else {
        a1a.spell_caster_pid = Pid();
    }

    int id = a1a.Create(0, 0, 0, 0);
    if (id != -1) {
        pushAoeAttack(PID(OBJECT_Item, id), a5, a1a.vPosition, ABILITY_ATTACK1);
    }
}

CombinedSkillValue Character::getSkillValue(CharacterSkillType skill) const {
    return pActiveSkills[skill];
}

void Character::setSkillValue(CharacterSkillType skill, const CombinedSkillValue &value) {
    pActiveSkills[skill] = value;
}

void Character::playReaction(CharacterSpeech speech, int a3) {
    int speechCount = 0;
    int expressionCount = 0;
    int pickedSoundID = 0;

    if (engine->config->settings.VoiceLevel.value() > 0) {
        for (int i = 0; i < speechVariants[speech].size(); i++) {
            if (speechVariants[speech][i]) {
                speechCount++;
            }
        }
        if (speechCount) {
            int pickedVariant = speechVariants[speech][vrng->random(speechCount)];
            int numberOfSubvariants = byte_4ECF08[pickedVariant - 1][uVoiceID];
            if (numberOfSubvariants > 0) {
                pickedSoundID = vrng->random(numberOfSubvariants) + 2 * (pickedVariant + 50 * uVoiceID) + 4998;
                pAudioPlayer->playSound((SoundID)pickedSoundID, PID(OBJECT_Character, getCharacterIndex()));
            }
        }
    }

    for (int i = 0; i < expressionVariants[speech].size(); i++) {
        if (expressionVariants[speech][i]) {
            expressionCount++;
        }
    }
    if (expressionCount) {
        CharacterExpressionID expression = (CharacterExpressionID)expressionVariants[speech][vrng->random(expressionCount)];
        int expressionDuration = 0;
        if (expression == CHARACTER_EXPRESSION_TALK && pickedSoundID) {
            if (pickedSoundID >= 0) {
                expressionDuration = (sLastTrackLengthMS << 7) / 1000;
            }
        }
        playEmotion(expression, expressionDuration);
    }
}

void Character::playEmotion(CharacterExpressionID new_expression, int duration) {
    // 38 - sparkles 1 character?

    unsigned int currexpr = expression;

    if (expression == CHARACTER_EXPRESSION_DEAD ||
        expression == CHARACTER_EXPRESSION_ERADICATED) {
        return;  // no react
    } else if (expression == CHARACTER_EXPRESSION_PERTIFIED &&
               new_expression != CHARACTER_EXPRESSION_FALLING) {
        return;  // no react
    } else {
        if (expression != CHARACTER_EXPRESSION_SLEEP ||
            new_expression != CHARACTER_EXPRESSION_FALLING) {
            if (currexpr >= 2 && currexpr <= 11 && currexpr != 8 &&
                !(new_expression == CHARACTER_EXPRESSION_DMGRECVD_MINOR ||
                  new_expression == CHARACTER_EXPRESSION_DMGRECVD_MODERATE ||
                  new_expression == CHARACTER_EXPRESSION_DMGRECVD_MAJOR)) {
                return;  // no react
            }
        }
    }

    this->uExpressionTimePassed = 0;

    if (!duration) {
        this->uExpressionTimeLength =
            8 * pPlayerFrameTable->pFrames[duration].uAnimLength;
    } else {
        this->uExpressionTimeLength = 0;
    }

    expression = new_expression;
}

bool Character::isClass(CharacterClassType class_type, bool check_honorary) const {
    if (classType == class_type) {
        return true;
    }

    if (!check_honorary) {
        return false;
    }

    switch (class_type) {
    case CHARACTER_CLASS_PRIEST_OF_SUN:
        return _achievedAwardsBits[Award_Promotion_PriestOfLight_Honorary];
    case CHARACTER_CLASS_PRIEST_OF_MOON:
        return _achievedAwardsBits[Award_Promotion_PriestOfDark_Honorary];
    case CHARACTER_CLASS_ARCHAMGE:
        return _achievedAwardsBits[Award_Promotion_Archmage_Honorary];
    case CHARACTER_CLASS_LICH:
        return _achievedAwardsBits[Award_Promotion_Lich_Honorary];
        break;
    default:
        Error("Should not be able to get here (%u)", class_type);
        break;
    }
    return false;
}

//----- (00490EEE) --------------------------------------------------------
MerchantPhrase Character::SelectPhrasesTransaction(ItemGen *pItem, BuildingType building_type, HOUSE_ID houseId, int ShopMenuType) {
    // TODO(_): probably move this somewhere else, not really Character:: stuff
    ITEM_TYPE idemId;   // edx@1
    ITEM_EQUIP_TYPE equipType;  // esi@1
    float multiplier;      // ST04_4@26
    int price;             // edi@26
    int merchantLevel;     // [sp+10h] [bp-8h]@1
    int itemValue;

    merchantLevel = getActualSkillValue(CHARACTER_SKILL_MERCHANT).level();
    idemId = pItem->uItemID;
    equipType = pItem->GetItemEquipType();
    itemValue = pItem->GetValue();

    switch (building_type) {
        case BUILDING_WEAPON_SHOP:
            if (idemId >= ITEM_ARTIFACT_HERMES_SANDALS)
                return MERCHANT_PHRASE_INVALID_ACTION;
            if (!isWeapon(equipType))
                return MERCHANT_PHRASE_INCOMPATIBLE_ITEM;
            break;
        case BUILDING_ARMOR_SHOP:
            if (idemId >= ITEM_ARTIFACT_HERMES_SANDALS)
                return MERCHANT_PHRASE_INVALID_ACTION;
            if (!isArmor(equipType))
                return MERCHANT_PHRASE_INCOMPATIBLE_ITEM;
            break;
        case BUILDING_MAGIC_SHOP:
            if (idemId >= ITEM_ARTIFACT_HERMES_SANDALS)
                return MERCHANT_PHRASE_INVALID_ACTION;
            if (pItemTable->pItems[idemId].uSkillType != CHARACTER_SKILL_MISC)
                return MERCHANT_PHRASE_INCOMPATIBLE_ITEM;
            break;
        case BUILDING_ALCHEMY_SHOP:
            if (idemId >= ITEM_ARTIFACT_HERMES_SANDALS && !isRecipe(idemId))
                return MERCHANT_PHRASE_INVALID_ACTION;
            if (equipType != EQUIP_REAGENT && equipType != EQUIP_POTION && equipType != EQUIP_MESSAGE_SCROLL)
                return MERCHANT_PHRASE_INCOMPATIBLE_ITEM;
            break;
        default:
            Error("(%u)", building_type);
            break;
    }
    if (pItem->IsStolen())
        return MERCAHNT_PHRASE_STOLEN_ITEM;

    multiplier = buildingTable[houseId].fPriceMultiplier;
    switch (ShopMenuType) {
        case 2:
            price = PriceCalculator::itemBuyingPriceForPlayer(this, itemValue, multiplier);
            break;
        case 3:
            // if (pItem->IsBroken())
            // price = 1;
            // else
            price = PriceCalculator::itemSellingPriceForPlayer(this, *pItem, multiplier);
            break;
        case 4:
            price = PriceCalculator::itemIdentificationPriceForPlayer(this, multiplier);
            break;
        case 5:
            price = PriceCalculator::itemRepairPriceForPlayer(this, itemValue, multiplier);
            break;
        default:
            Error("(%u)", ShopMenuType);
            break;
    }
    if (merchantLevel) {
        if (price == itemValue) {
            return MERCHANT_PHRASE_PRICE_HAGGLE_TO_ACTUAL_PRICE;
        } else {
            return MERCHANT_PHRASE_PRICE_HAGGLE;
        }
    } else {
        return MERCHANT_PHRASE_PRICE;
    }
}

//----- (0048C6AF) --------------------------------------------------------
Character::Character() {
    memset(&pEquipment, 0, sizeof(CharacterEquipment));
    pInventoryMatrix.fill(0);
    for (uint i = 0; i < INVENTORY_SLOT_COUNT; ++i) pInventoryItemList[i].Reset();
    for (uint i = 0; i < ADDITIONAL_SLOT_COUNT; ++i) pEquippedItems[i].Reset();

    for (auto &buf : pCharacterBuffs) {
        buf.Reset();
    }

    name[0] = 0;
    uCurrentFace = 0;
    uVoiceID = 0;
    conditions.ResetAll();

    uMight = uMightBonus = 0;
    uIntelligence = uIntelligenceBonus = 0;
    uPersonality = uPersonalityBonus = 0;
    uEndurance = uEnduranceBonus = 0;
    uSpeed = uSpeedBonus = 0;
    uAccuracy = uAccuracyBonus = 0;
    uLuck = uLuckBonus = 0;
    uLevel = sLevelModifier = 0;
    sAgeModifier = 0;
    sACModifier = 0;

    //  memset(field_1F5, 0, 30);
    pure_luck_used = 0;
    pure_speed_used = 0;
    pure_intellect_used = 0;
    pure_endurance_used = 0;
    pure_personality_used = 0;
    pure_accuracy_used = 0;
    pure_might_used = 0;

    sResFireBase = sResFireBonus = 0;
    sResAirBase = sResAirBonus = 0;
    sResWaterBase = sResWaterBonus = 0;
    sResEarthBase = sResEarthBonus = 0;
    sResMagicBase = sResMagicBonus = 0;
    sResSpiritBase = sResSpiritBonus = 0;
    sResMindBase = sResMindBonus = 0;
    sResBodyBase = sResBodyBonus = 0;
    sResLightBase = sResLightBonus = 0;
    sResDarkBase = sResDarkBonus = 0;

    timeToRecovery = 0;

    uSkillPoints = 0;

    health = 0;
    uFullHealthBonus = 0;
    _health_related = 0;

    mana = 0;
    uFullManaBonus = 0;
    _mana_related = 0;

    uQuickSpell = SPELL_NONE;

    _some_attack_bonus = 0;
    _melee_dmg_bonus = 0;
    _ranged_atk_bonus = 0;
    _ranged_dmg_bonus = 0;

    expression = CHARACTER_EXPRESSION_INVALID;
    uExpressionTimePassed = 0;
    uExpressionTimeLength = 0;

    uNumDivineInterventionCastsThisDay = 0;
    uNumArmageddonCasts = 0;
    uNumFireSpikeCasts = 0;

    _characterEventBits.reset();

    field_E0 = 0;
    field_E4 = 0;
    field_E8 = 0;
    field_EC = 0;
    field_F0 = 0;
    field_F4 = 0;
    field_F8 = 0;
    field_FC = 0;
    field_100 = 0;
    field_104 = 0;

    _expression21_animtime = 0;
    _expression21_frameset = 0;

    lastOpenedSpellbookPage = 0;
}

void Character::cleanupBeacons() {
    struct delete_beacon {
        bool operator()(const LloydBeacon &beacon) const {
            return (beacon.uBeaconTime < pParty->GetPlayingTime());
        }
    };
    vBeacons.erase(std::remove_if(vBeacons.begin(), vBeacons.end(),
        [](const LloydBeacon &beacon) {
            return (beacon.uBeaconTime < pParty->GetPlayingTime());
        }), vBeacons.end()
    );
}

bool Character::setBeacon(int index, GameTime duration) {
    MAP_TYPE file_index = pMapStats->GetMapInfo(pCurrentMapName);
    if (file_index == MAP_INVALID) {
        return false;
    }

    LloydBeacon beacon;

    beacon.image = render->TakeScreenshot(92, 68);
    beacon.uBeaconTime = pParty->GetPlayingTime() + duration;
    beacon.PartyPos_X = pParty->pos.x;
    beacon.PartyPos_Y = pParty->pos.y;
    beacon.PartyPos_Z = pParty->pos.z;
    beacon._partyViewYaw = pParty->_viewYaw;
    beacon._partyViewPitch = pParty->_viewPitch;
    beacon.mapId = file_index;

    if (index < vBeacons.size()) {
        // overwrite so clear image
        vBeacons[index].image->Release();
        vBeacons[index] = beacon;
    } else {
        vBeacons.push_back(beacon);
    }

    return true;
}
