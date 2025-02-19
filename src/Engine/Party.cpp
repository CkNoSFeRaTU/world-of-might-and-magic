#include "Engine/Party.h"

#include <cstdlib>
#include <climits>
#include <algorithm>
#include <string>

#include "Engine/Engine.h"
#include "Engine/Graphics/Outdoor.h"
#include "Engine/Graphics/Indoor.h"
#include "Engine/Graphics/Image.h"
#include "Engine/Localization.h"
#include "Engine/Objects/Actor.h"
#include "Engine/Objects/ObjectList.h"
#include "Engine/Objects/SpriteObject.h"
#include "Engine/Objects/NPC.h"
#include "Engine/Tables/ItemTable.h"
#include "Engine/Tables/IconFrameTable.h"
#include "Engine/Tables/CharacterFrameTable.h"
#include "Engine/Time.h"
#include "Engine/TurnEngine/TurnEngine.h"
#include "Engine/OurMath.h"
#include "Engine/AssetsManager.h"
#include "Engine/MapInfo.h"

#include "GUI/GUIWindow.h"
#include "GUI/GUIMessageQueue.h"
#include "GUI/UI/UIStatusBar.h"
#include "GUI/UI/UIRest.h"

#include "Io/Mouse.h"

#include "Media/Audio/AudioPlayer.h"

#include "Library/Random/Random.h"
#include "Library/Logger/Logger.h"

using Io::Mouse;

Party *pParty = nullptr;

struct ActionQueue *pPartyActionQueue = new ActionQueue;

std::array<bool, 4> playerAlreadyPicked;  // byte_AE3368_
char PickedPlayer2_unused;                // byte_AE3369_
char PickedPlayer3_unused;                // byte_AE336A_
char PickedPlayer4_unused;                // byte_AE336B_

struct {
    UIAnimation _pUIAnim_Food;
    UIAnimation _pUIAnim_Gold;
    UIAnimation _pUIAnum_Torchlight;
    UIAnimation _pUIAnim_WizardEye;
} _uianim;

UIAnimation *pUIAnim_Food = &_uianim._pUIAnim_Food;
UIAnimation *pUIAnim_Gold = &_uianim._pUIAnim_Gold;
UIAnimation *pUIAnum_Torchlight = &_uianim._pUIAnum_Torchlight;
UIAnimation *pUIAnim_WizardEye = &_uianim._pUIAnim_WizardEye;

std::array<class UIAnimation *, 4>
    pUIAnims =  // was struct byt defined as class
    {&_uianim._pUIAnim_Food, &_uianim._pUIAnim_Gold,
     &_uianim._pUIAnum_Torchlight, &_uianim._pUIAnim_WizardEye};

//----- (0044A56A) --------------------------------------------------------
int Party::CountHirelings() {  // non hired followers
    cNonHireFollowers = 0;

    for (unsigned int i = 0; i < pNPCStats->uNumNewNPCs; ++i) {
        NPCData *npc = &pNPCStats->pNewNPCData[i];
        if (npc->Hired() && npc->pName != pHirelings[0].pName && npc->pName != pHirelings[1].pName)
            ++cNonHireFollowers;
    }

    return cNonHireFollowers;
}

// inlined
//----- (mm6c::004858D0) --------------------------------------------------
void Party::Zero() {
    height = defaultHeight = engine->config->gameplay.PartyHeight.value();
    eyeLevel = defaultEyeLevel = engine->config->gameplay.PartyEyeLevel.value();
    radius = 37;
    _yawGranularity = 25;
    walkSpeed = engine->config->gameplay.PartyWalkSpeed.value();
    _yawRotationSpeed = 90;
    jump_strength = 5;
    playing_time = GameTime(0, 0, 0);
    last_regenerated = GameTime(0, 0, 0);
    PartyTimes.bountyHuntNextGenTime.fill(GameTime(0));
    PartyTimes.CounterEventValues.fill(GameTime(0));
    PartyTimes.HistoryEventTimes.fill(GameTime(0));
    PartyTimes.shopNextRefreshTime.fill(GameTime(0));
    PartyTimes.guildNextRefreshTime.fill(GameTime(0));
    PartyTimes.shopBanTimes.fill(GameTime(0));
    PartyTimes._s_times.fill(GameTime(0));
    pos = lastPos = Vec3i();
    speed = Vec3i();
    _viewYaw = _viewPrevYaw = 0;
    _viewPitch = _viewPrevPitch = 0;
    lastEyeLevel = 0;
    sPartySavedFlightZ = 0;
    floor_face_id = 0;
    currentWalkingSound = SOUND_Invalid;
    _6FC_water_lava_timer = 0;
    uFallStartZ = 0;
    bFlying = 0;
    hirelingScrollPosition = 0;
    cNonHireFollowers = 0;
    uCurrentYear = 0;
    uCurrentMonth = 0;
    uCurrentMonthWeek = 0;
    uCurrentDayOfMonth = 0;
    uCurrentHour = 0;
    uCurrentMinute = 0;
    uCurrentTimeSecond = 0;
    uNumFoodRations = 0;
    uNumGold = 0;
    uNumGoldInBank = 0;
    uNumDeaths = 0;
    uNumPrisonTerms = 0;
    uNumBountiesCollected = 0;
    monster_id_for_hunting.fill(0);
    monster_for_hunting_killed.fill(false);
    days_played_without_rest = 0;
    _questBits.reset();
    pArcomageWins.fill(0);
    field_7B5_in_arena_quest = 0;
    uNumArenaWins.fill(0);
    pIsArtifactFound.fill(false);
    _autonoteBits.reset();
    uNumArcomageWins = 0;
    uNumArcomageLoses = 0;
    bTurnBasedModeOn = false;
    uFlags2 = 0;
    alignment = PartyAlignment::PartyAlignment_Neutral;
    for (uint i = 0; i < 20; ++i) pPartyBuffs[i].Reset();
    pPickedItem.Reset();
    uFlags = 0;

    for (HOUSE_ID i : standartItemsInShops.indices())
        for (int j = 0; j < 12; ++j)
            standartItemsInShops[i][j].Reset();

    for (HOUSE_ID i : specialItemsInShops.indices())
        for (int j = 0; j < 12; ++j)
            specialItemsInShops[i][j].Reset();

    for (HOUSE_ID i : spellBooksInGuilds.indices())
        for (int j = 0; j < 12; ++j)
            spellBooksInGuilds[i][j].Reset();

    pHireling1Name[0] = 0;
    pHireling2Name[0] = 0;
    armageddon_timer = 0;
    armageddonDamage = 0;
    pTurnBasedCharacterRecoveryTimes.fill(0);
    InTheShopFlags.fill(0);
    uFine = 0;
    TorchLightLastIntensity = 0.0f;

    _roundingDt = 0;

    // players
    for (Character &player : this->pCharacters) {
        player.resetTempBonuses();
        player.sResFireBase = 0;
        player.sResAirBase = 0;
        player.sResWaterBase = 0;
        player.sResEarthBase = 0;
        player.sResPhysicalBase = 0;
        player.sResMagicBase = 0;
        player.sResSpiritBase = 0;
        player.sResMindBase = 0;
        player.sResBodyBase = 0;
        player.sResLightBase = 0;
        player.sResDarkBase = 0;

        for (int z = 0; z < player.vBeacons.size(); z++) {
            player.vBeacons[z].image->Release();
        }
        player.vBeacons.clear();
    }

    // hirelings
    pHirelings.fill(NPCData());

    playerAlreadyPicked.fill(false); // TODO(captainurist): belongs in a different place?
}

// inlined
//----- (mm6c::0045BE90) --------------------------------------------------
void ActionQueue::Reset() { uNumActions = 0; }

//----- (004760C1) --------------------------------------------------------
void ActionQueue::Add(PartyAction action) {
    if (uNumActions < 30) pActions[uNumActions++] = action;
}

//----- (00497FC5) --------------------------------------------------------
bool Party::_497FC5_check_party_perception_against_level() {
    int uMaxPerception;  // edi@1
    signed int v5;       // eax@3
    bool result;         // eax@7

    uMaxPerception = 0;
    for (Character &player : this->pCharacters) {
        if (player.CanAct()) {
            v5 = player.GetPerception();
            if (v5 > uMaxPerception) uMaxPerception = v5;
        }
    }
    if (uLevelMapStatsID >= MAP_FIRST && uLevelMapStatsID <= MAP_LAST)
        result = uMaxPerception >= 2 * pMapStats->pInfos[uLevelMapStatsID]._per;
    else
        result = 0;
    return result;
}

void Party::setHoldingItem(ItemGen *pItem) {
    placeHeldItemInInventoryOrDrop();
    pPickedItem = *pItem;
    mouse->SetCursorBitmapFromItemID(pPickedItem.uItemID);
}

void Party::setActiveToFirstCanAct() {  // added to fix some nzi problems entering shops
    for (int i = 0; i < this->pCharacters.size(); ++i) {
        if (this->pCharacters[i].CanAct()) {
            _activeCharacter = i + 1;
            return;
        }
    }

    assert(false);  // should not get here
}

//----- (0049370F) --------------------------------------------------------
void Party::switchToNextActiveCharacter() {
    // avoid switching away from char that can act
    if (hasActiveCharacter() && this->pCharacters[_activeCharacter - 1].CanAct() &&
        this->pCharacters[_activeCharacter - 1].timeToRecovery < 1)
        return;

    if (pParty->bTurnBasedModeOn) {
        if (pTurnEngine->turn_stage != TE_ATTACK || PID_TYPE(pTurnEngine->pQueue[0].uPackedID) != OBJECT_Character) {
            _activeCharacter = 0;
        } else {
            _activeCharacter = PID_ID(pTurnEngine->pQueue[0].uPackedID) + 1;
        }
        return;
    }

    if (playerAlreadyPicked[0] && playerAlreadyPicked[1] &&
        playerAlreadyPicked[2] && playerAlreadyPicked[3])
        playerAlreadyPicked.fill(false);

    for (int i = 0; i < this->pCharacters.size(); i++) {
        if (!this->pCharacters[i].CanAct() ||
            this->pCharacters[i].timeToRecovery > 0) {
            playerAlreadyPicked[i] = true;
        } else if (!playerAlreadyPicked[i]) {
            playerAlreadyPicked[i] = true;
            if (i > 0) { // TODO(_) check if this condition really should be here. it is
                // equal to the original source but still seems kind of weird
                _activeCharacter = i + 1;
                return;
            }
            break;
        }
    }

    int v12{};
    uint v8{};
    for (int i = 0; i < this->pCharacters.size(); i++) {
        if (this->pCharacters[i].CanAct() &&
            this->pCharacters[i].timeToRecovery == 0) {
            if (v12 == 0 || this->pCharacters[i].uSpeedBonus > v8) {
                v8 = this->pCharacters[i].uSpeedBonus;
                v12 = i + 1;
            }
        }
    }
    _activeCharacter = v12;
    return;
}

bool Party::hasItem(ITEM_TYPE uItemID) {
    for (Character &player : this->pCharacters) {
        for (ItemGen &item : player.pOwnItems) {
            if (item.uItemID == uItemID)
                return true;
        }
    }
    return false;
}

void ui_play_gold_anim() {
    pUIAnim_Gold->uAnimTime = 0;
    pUIAnim_Gold->uAnimLength = pUIAnim_Gold->icon->GetAnimLength();
    pAudioPlayer->playUISound(SOUND_gold01);
}

void ui_play_food_anim() {
    pUIAnim_Food->uAnimTime = 0;
    pUIAnim_Food->uAnimLength = pUIAnim_Food->icon->GetAnimLength();
    // pAudioPlayer->PlaySound(SOUND_eat, 0, 0, -1, 0, 0);
}

//----- (00492AD5) --------------------------------------------------------
void Party::SetFood(int amount) {
    if (amount > 65535)
        amount = 65535;
    else if (amount < 0)
        amount = 0;

    uNumFoodRations = amount;

    ui_play_food_anim();
}

//----- (00492B03) --------------------------------------------------------
void Party::TakeFood(int amount) {
    SetFood(GetFood() - amount);
}

//----- (00492B42) --------------------------------------------------------
void Party::GiveFood(int amount) {
    SetFood(GetFood() + amount);
}

int Party::GetFood() const {
    if (engine->config->debug.InfiniteFood.value()) {
        return 99999;
    }

    return uNumFoodRations;
}

int Party::GetGold() const {
    if (engine->config->debug.InfiniteGold.value()) {
        return 99999;
    }

    return uNumGold;
}

//----- (00492B70) --------------------------------------------------------
void Party::SetGold(int amount, bool silent) {
    if (amount < 0)
        amount = 0;

    uNumGold = amount;

    if (!silent) ui_play_gold_anim();
}

void Party::AddGold(int amount) {
    SetGold(GetGold() + amount);
}

//----- (00492BB6) --------------------------------------------------------
void Party::TakeGold(int amount, bool silent) {
    SetGold(GetGold() - amount, silent);
}

int Party::GetBankGold() const {
    return uNumGoldInBank;
}

void Party::SetBankGold(int amount) {
    if (amount < 0)
        amount = 0;

    uNumGoldInBank = amount;
}

void Party::AddBankGold(int amount) {
    SetBankGold(GetBankGold() + amount);
}

void Party::TakeBankGold(int amount) {
    SetBankGold(GetBankGold() - amount);
}

int Party::GetFine() const {
    return uFine;
}

void Party::SetFine(int amount) {
    if (amount < 0)
        amount = 0;

    uFine = amount;
}

void Party::AddFine(int amount) {
    SetFine(GetFine() + amount);
}

void Party::TakeFine(int amount) {
    SetFine(GetFine() - amount);
}

//----- (0049135E) --------------------------------------------------------
unsigned int Party::getPartyFame() {
    uint64_t total_exp = 0;
    for (Character &player : this->pCharacters) {
        total_exp += player.experience;
    }
    return std::min(
        (unsigned int)(total_exp / 1000),
        UINT_MAX);  // min wasn't present, but could be incorrect without it
}

void Party::createDefaultParty(bool bDebugGiveItems) {
    signed int uNumPlayers;  // [sp+18h] [bp-28h]@1
    ItemGen Dst;             // [sp+1Ch] [bp-24h]@10

    pHireling1Name[0] = 0;
    pHireling2Name[0] = 0;
    this->hirelingScrollPosition = 0;
    pHirelings.fill(NPCData());

    this->pCharacters[0].name = localization->GetString(LSTR_PC_NAME_ZOLTAN);
    this->pCharacters[0].uPrevFace = 17;
    this->pCharacters[0].uCurrentFace = 17;
    this->pCharacters[0].uPrevVoiceID = 17;
    this->pCharacters[0].uVoiceID = 17;
    this->pCharacters[0].uMight = 30;
    this->pCharacters[0].uIntelligence = 5;
    this->pCharacters[0].uPersonality = 5;
    this->pCharacters[0].uEndurance = 13;
    this->pCharacters[0].uAccuracy = 13;
    this->pCharacters[0].uSpeed = 14;
    this->pCharacters[0].uLuck = 7;
    this->pCharacters[0].pActiveSkills[CHARACTER_SKILL_LEATHER] = CombinedSkillValue::novice();
    this->pCharacters[0].pActiveSkills[CHARACTER_SKILL_ARMSMASTER] = CombinedSkillValue::novice();
    this->pCharacters[0].pActiveSkills[CHARACTER_SKILL_BOW] = CombinedSkillValue::novice();
    this->pCharacters[0].pActiveSkills[CHARACTER_SKILL_SWORD] = CombinedSkillValue::novice();

    this->pCharacters[1].name = localization->GetString(LSTR_PC_NAME_RODERIC);
    this->pCharacters[1].uPrevFace = 3;
    this->pCharacters[1].uCurrentFace = 3;
    this->pCharacters[1].uPrevVoiceID = 3;
    this->pCharacters[1].uVoiceID = 3;
    this->pCharacters[1].uMight = 13;
    this->pCharacters[1].uIntelligence = 9;
    this->pCharacters[1].uPersonality = 9;
    this->pCharacters[1].uEndurance = 13;
    this->pCharacters[1].uAccuracy = 13;
    this->pCharacters[1].uSpeed = 13;
    this->pCharacters[1].uLuck = 13;
    this->pCharacters[1].pActiveSkills[CHARACTER_SKILL_LEATHER] = CombinedSkillValue::novice();
    this->pCharacters[1].pActiveSkills[CHARACTER_SKILL_STEALING] = CombinedSkillValue::novice();
    this->pCharacters[1].pActiveSkills[CHARACTER_SKILL_DAGGER] = CombinedSkillValue::novice();
    this->pCharacters[1].pActiveSkills[CHARACTER_SKILL_TRAP_DISARM] = CombinedSkillValue::novice();

    this->pCharacters[2].name = localization->GetString(LSTR_PC_NAME_SERENA);
    this->pCharacters[2].uPrevFace = 14;
    this->pCharacters[2].uCurrentFace = 14;
    this->pCharacters[2].uPrevVoiceID = 14;
    this->pCharacters[2].uVoiceID = 14;
    this->pCharacters[2].uMight = 12;
    this->pCharacters[2].uIntelligence = 9;
    this->pCharacters[2].uPersonality = 20;
    this->pCharacters[2].uEndurance = 22;
    this->pCharacters[2].uAccuracy = 7;
    this->pCharacters[2].uSpeed = 13;
    this->pCharacters[2].uLuck = 7;
    this->pCharacters[2].pActiveSkills[CHARACTER_SKILL_ALCHEMY] = CombinedSkillValue::novice();
    this->pCharacters[2].pActiveSkills[CHARACTER_SKILL_LEATHER] = CombinedSkillValue::novice();
    this->pCharacters[2].pActiveSkills[CHARACTER_SKILL_BODY] = CombinedSkillValue::novice();
    this->pCharacters[2].pActiveSkills[CHARACTER_SKILL_MACE] = CombinedSkillValue::novice();

    this->pCharacters[3].name = localization->GetString(LSTR_PC_NAME_ALEXIS);
    this->pCharacters[3].uPrevFace = 10;
    this->pCharacters[3].uCurrentFace = 10;
    this->pCharacters[3].uEndurance = 13;
    this->pCharacters[3].uAccuracy = 13;
    this->pCharacters[3].uSpeed = 13;
    this->pCharacters[3].uPrevVoiceID = 10;
    this->pCharacters[3].uVoiceID = 10;
    this->pCharacters[3].uMight = 5;
    this->pCharacters[3].uIntelligence = 30;
    this->pCharacters[3].uPersonality = 9;
    this->pCharacters[3].uLuck = 7;
    this->pCharacters[3].pActiveSkills[CHARACTER_SKILL_LEATHER] = CombinedSkillValue::novice();
    this->pCharacters[3].pActiveSkills[CHARACTER_SKILL_AIR] = CombinedSkillValue::novice();
    this->pCharacters[3].pActiveSkills[CHARACTER_SKILL_FIRE] = CombinedSkillValue::novice();
    this->pCharacters[3].pActiveSkills[CHARACTER_SKILL_STAFF] = CombinedSkillValue::novice();

    for (Character &pCharacter : pCharacters) {
        if (pCharacter.classType == CHARACTER_CLASS_KNIGHT)
            pCharacter.sResMagicBase = 10;

        pCharacter.lastOpenedSpellbookPage = 0;
        int count = 0;
        for (CharacterSkillType skill : allMagicSkills()) {  // for Magic Book
            if (pCharacter.pActiveSkills[skill]) {
                pCharacter.lastOpenedSpellbookPage = count;
                break;
            }

            count++;
        }

        pCharacter.uExpressionTimePassed = 0;

        if (bDebugGiveItems) {
            Dst.Reset();
            pItemTable->generateItem(ITEM_TREASURE_LEVEL_2, 40, &Dst);  // ring
            pCharacter.AddItem2(-1, &Dst);
            for (int uSkillIdx = 0; uSkillIdx < 36; uSkillIdx++) {
                CharacterSkillType skill = (CharacterSkillType)uSkillIdx;
                if (pCharacter.pActiveSkills[skill]) {
                    switch (skill) {
                        case CHARACTER_SKILL_STAFF:
                            pCharacter.WearItem(ITEM_STAFF);
                            break;
                        case CHARACTER_SKILL_SWORD:
                            pCharacter.WearItem(ITEM_CRUDE_LONGSWORD);
                            break;
                        case CHARACTER_SKILL_DAGGER:
                            pCharacter.WearItem(ITEM_DAGGER);
                            break;
                        case CHARACTER_SKILL_AXE:
                            pCharacter.WearItem(ITEM_CRUDE_AXE);
                            break;
                        case CHARACTER_SKILL_SPEAR:
                            pCharacter.WearItem(ITEM_CRUDE_SPEAR);
                            break;
                        case CHARACTER_SKILL_BOW:
                            pCharacter.WearItem(ITEM_CROSSBOW);
                            break;
                        case CHARACTER_SKILL_MACE:
                            pCharacter.WearItem(ITEM_MACE);
                            break;
                        case CHARACTER_SKILL_SHIELD:
                            pCharacter.WearItem(ITEM_WOODEN_BUCKLER);
                            break;
                        case CHARACTER_SKILL_LEATHER:
                            pCharacter.WearItem(ITEM_LEATHER_ARMOR);
                            break;
                        case CHARACTER_SKILL_CHAIN:
                            pCharacter.WearItem(ITEM_CHAIN_MAIL);
                            break;
                        case CHARACTER_SKILL_PLATE:
                            pCharacter.WearItem(ITEM_PLATE_ARMOR);
                            break;
                        case CHARACTER_SKILL_FIRE:
                            pCharacter.AddItem(-1, ITEM_SPELLBOOK_FIRE_BOLT);
                            break;
                        case CHARACTER_SKILL_AIR:
                            pCharacter.AddItem(-1, ITEM_SPELLBOOK_FEATHER_FALL);
                            break;
                        case CHARACTER_SKILL_WATER:
                            pCharacter.AddItem(-1, ITEM_SPELLBOOK_POISON_SPRAY);
                            break;
                        case CHARACTER_SKILL_EARTH:
                            pCharacter.AddItem(-1, ITEM_SPELLBOOK_SLOW);
                            break;
                        case CHARACTER_SKILL_SPIRIT:
                            pCharacter.AddItem(-1, ITEM_SPELLBOOK_BLESS);
                            break;
                        case CHARACTER_SKILL_MIND:
                            pCharacter.AddItem(-1, ITEM_SPELLBOOK_MIND_BLAST);
                            break;
                        case CHARACTER_SKILL_BODY:
                            pCharacter.AddItem(-1, ITEM_SPELLBOOK_HEAL);
                            break;
                        case CHARACTER_SKILL_ITEM_ID:
                        case CHARACTER_SKILL_REPAIR:
                        case CHARACTER_SKILL_MEDITATION:
                        case CHARACTER_SKILL_PERCEPTION:
                        case CHARACTER_SKILL_DIPLOMACY:
                        case CHARACTER_SKILL_TRAP_DISARM:
                        case CHARACTER_SKILL_LEARNING:
                            pCharacter.AddItem(-1, ITEM_POTION_BOTTLE);
                            pCharacter.AddItem(-1, grng->randomSample(allLevel1Reagents())); // Add simple reagent.
                            break;
                        case CHARACTER_SKILL_DODGE:
                            pCharacter.AddItem(-1, ITEM_LEATHER_BOOTS);
                            break;
                        case CHARACTER_SKILL_UNARMED:
                            pCharacter.AddItem(-1, ITEM_GAUNTLETS);
                            break;
                        default:
                            break;
                    }
                }
            }
            for (int i = 0; i < Character::INVENTORY_SLOT_COUNT; i++) {
                if (pCharacter.pInventoryItemList[i].uItemID != ITEM_NULL) {
                    pCharacter.pInventoryItemList[i].SetIdentified();
                }
            }
            for (int i = 0; i < Character::ADDITIONAL_SLOT_COUNT; i++) {
                if (pCharacter.pEquippedItems[i].uItemID != ITEM_NULL) {
                    pCharacter.pEquippedItems[i].SetIdentified();
                }
            }
        }

        pCharacter.health = pCharacter.GetMaxHealth();
        pCharacter.mana = pCharacter.GetMaxMana();
    }
}

//----- (004917CE) --------------------------------------------------------
void Party::Reset() {
    Zero();

    eyeLevel = engine->config->gameplay.PartyEyeLevel.value();
    uNumGold = engine->config->gameplay.NewGameGold.value();
    uNumFoodRations = engine->config->gameplay.NewGameFood.value();

    alignment = PartyAlignment::PartyAlignment_Neutral;
    SetUserInterface(alignment, true);

    // game begins at 9 am
    this->playing_time = GameTime(0, 0, 9);
    this->last_regenerated = GameTime(0, 0, 9);
    this->uCurrentHour = 9;

    bTurnBasedModeOn = false;

    pParty->_activeCharacter = 1;

    pCharacters[0].Reset(CHARACTER_CLASS_KNIGHT);
    pCharacters[0].uCurrentFace = 17;
    pCharacters[0].uPrevVoiceID = 17;
    pCharacters[0].uVoiceID = 17;
    pCharacters[0].SetInitialStats();
    pCharacters[0].uSex = pCharacters[0].GetSexByVoice();
    pCharacters[0].name = localization->GetString(LSTR_PC_NAME_ZOLTAN);

    pCharacters[1].Reset(CHARACTER_CLASS_THIEF);
    pCharacters[1].uCurrentFace = 3;
    pCharacters[1].uPrevVoiceID = 3;
    pCharacters[1].uVoiceID = 3;
    pCharacters[1].SetInitialStats();
    pCharacters[1].uSex = pCharacters[1].GetSexByVoice();
    pCharacters[1].name = localization->GetString(LSTR_PC_NAME_RODERIC);

    pCharacters[2].Reset(CHARACTER_CLASS_CLERIC);
    pCharacters[2].uCurrentFace = 14;
    pCharacters[2].uPrevVoiceID = 14;
    pCharacters[2].uVoiceID = 14;
    pCharacters[2].SetInitialStats();
    pCharacters[2].uSex = pCharacters[3].GetSexByVoice();
    pCharacters[2].name = localization->GetString(LSTR_PC_NAME_SERENA);

    pCharacters[3].Reset(CHARACTER_CLASS_SORCERER);
    pCharacters[3].uCurrentFace = 10;
    pCharacters[3].uPrevVoiceID = 10;
    pCharacters[3].uVoiceID = 10;
    pCharacters[3].SetInitialStats();
    pCharacters[3].uSex = pCharacters[3].GetSexByVoice();
    pCharacters[3].name = localization->GetString(LSTR_PC_NAME_ALEXIS);

    for (Character &player : this->pCharacters) {
        player.timeToRecovery = 0;
        player.conditions.ResetAll();

        for (SpellBuff &buff : player.pCharacterBuffs) {
            buff.Reset();
        }

        player.expression = CHARACTER_EXPRESSION_NORMAL;
        player.uExpressionTimePassed = 0;
        player.uExpressionTimeLength = vrng->random(256) + 128;
    }

    for (SpellBuff &buff : this->pPartyBuffs) {
        buff.Reset();
    }

    current_character_screen_window = WINDOW_CharacterWindow_Stats;  // default character ui - stats
    uFlags = 0;
    _autonoteBits.reset();

    _questBits.reset();
    pParty->_questBits.set(QBIT_EMERALD_ISLAND_RED_POTION_ACTIVE);
    pParty->_questBits.set(QBIT_EMERALD_ISLAND_SEASHELL_ACTIVE);
    pParty->_questBits.set(QBIT_EMERALD_ISLAND_LONGBOW_ACTIVE);
    pParty->_questBits.set(QBIT_EMERALD_ISLAND_PLATE_ACTIVE);
    pParty->_questBits.set(QBIT_EMERALD_ISLAND_LUTE_ACTIVE);
    pParty->_questBits.set(QBIT_EMERALD_ISLAND_HAT_ACTIVE);

    pIsArtifactFound.fill(false);

    PartyTimes.shopBanTimes.fill(GameTime(0));

    pNPCStats->pNewNPCData = pNPCStats->pNPCData;
    pNPCStats->pGroups_copy = pNPCStats->pGroups;
    pNPCStats->pNewNPCData[3].uFlags |= NPC_HIRED;  //|= 0x80u; Lady Margaret
    _494035_timed_effects__water_walking_damage__etc();
    pEventTimer->Pause();

    this->pPickedItem.uItemID = ITEM_NULL;
}

void Party::yell() {
    if (pParty->pPartyBuffs[PARTY_BUFF_INVISIBILITY].Active()) {
        pParty->pPartyBuffs[PARTY_BUFF_INVISIBILITY].Reset();
    }

    if (!pParty->bTurnBasedModeOn) {
        for (int i = 0; i < pActors.size(); i++) {
            Actor &actor = pActors[i];
            if (actor.CanAct() &&
                actor.monsterInfo.uHostilityType != MonsterInfo::Hostility_Long &&
                actor.monsterInfo.uMovementType != MONSTER_MOVEMENT_TYPE_STATIONARY) {
                if ((actor.pos - pParty->pos).length() < 512) {
                    Actor::AI_Flee(i, Pid::character(0), 0, 0);
                }
            }
        }
    }
}

//----- (00491BF9) --------------------------------------------------------
void Party::ResetPosMiscAndSpellBuffs() {
    this->pos = Vec3i();
    this->speed = Vec3i();
    this->uFallStartZ = 0;
    this->_viewYaw = 0;
    this->_viewPitch = 0;
    this->defaultHeight = engine->config->gameplay.PartyHeight.value(); // was 120?
    this->radius = 37;
    this->_yawGranularity = 25;
    this->walkSpeed = engine->config->gameplay.PartyWalkSpeed.value();
    this->_yawRotationSpeed = 90;
    this->jump_strength = 5;
    this->_6FC_water_lava_timer = 0;

    for (Character &player : this->pCharacters) {
        for (SpellBuff &buff : player.pCharacterBuffs) {
            buff.Reset();
        }
    }
    for (SpellBuff &buff : this->pPartyBuffs) {
        buff.Reset();
    }
}

void Party::resetCharacterEmotions() {
    for (Character &player : this->pCharacters) {
        Condition condition = player.GetMajorConditionIdx();
        if (condition == CONDITION_GOOD || condition == CONDITION_ZOMBIE) {
            player.uExpressionTimeLength = 32;
            player.expression = CHARACTER_EXPRESSION_NORMAL;
        } else {
            player.uExpressionTimeLength = 0;
            player.uExpressionTimePassed = 0;
            player.expression = expressionForCondition(condition);
        }
    }
}

void Party::updateCharactersAndHirelingsEmotions() {
    if (pParty->cNonHireFollowers < 0) {
        pParty->CountHirelings();
    }

    for (Character &player : this->pCharacters) {
        player.uExpressionTimePassed += (unsigned short)pMiscTimer->uTimeElapsed;

        Condition condition = player.GetMajorConditionIdx();
        if (condition == CONDITION_GOOD || condition == CONDITION_ZOMBIE) {
            if (player.uExpressionTimePassed < player.uExpressionTimeLength)
                continue;

            player.uExpressionTimePassed = 0;
            if (player.expression != CHARACTER_EXPRESSION_NORMAL || vrng->random(5)) {
                player.expression = CHARACTER_EXPRESSION_NORMAL;
                player.uExpressionTimeLength = vrng->random(256) + 32;
            } else {
                int randomVal = vrng->random(100);
                if (randomVal < 25)
                    player.expression = CHARACTER_EXPRESSION_BLINK;
                else if (randomVal < 31)
                    player.expression = CHARACTER_EXPRESSION_WINK;
                else if (randomVal < 37)
                    player.expression = CHARACTER_EXPRESSION_MOUTH_OPEN_RANDOM;
                else if (randomVal < 43)
                    player.expression = CHARACTER_EXPRESSION_PURSE_LIPS_RANDOM;
                else if (randomVal < 46)
                    player.expression = CHARACTER_EXPRESSION_LOOK_UP;
                else if (randomVal < 52)
                    player.expression = CHARACTER_EXPRESSION_LOOK_RIGHT;
                else if (randomVal < 58)
                    player.expression = CHARACTER_EXPRESSION_LOOK_LEFT;
                else if (randomVal < 64)
                    player.expression = CHARACTER_EXPRESSION_LOOK_DOWN;
                else if (randomVal < 70)
                    player.expression = CHARACTER_EXPRESSION_54;
                else if (randomVal < 76)
                    player.expression = CHARACTER_EXPRESSION_55;
                else if (randomVal < 82)
                    player.expression = CHARACTER_EXPRESSION_56;
                else if (randomVal < 88)
                    player.expression = CHARACTER_EXPRESSION_57;
                else if (randomVal < 94)
                    player.expression = CHARACTER_EXPRESSION_PURSE_LIPS_1;
                else
                    player.expression = CHARACTER_EXPRESSION_PURSE_LIPS_2;
            }

            for (size_t j = 0; j < pPlayerFrameTable->pFrames.size(); ++j) {
                PlayerFrame *frame = &pPlayerFrameTable->pFrames[j];
                if (frame->expression == player.expression) {
                    player.uExpressionTimeLength = 8 * frame->uAnimLength;
                    break;
                }
            }
        } else if (player.expression != CHARACTER_EXPRESSION_DMGRECVD_MINOR &&
                   player.expression != CHARACTER_EXPRESSION_DMGRECVD_MODERATE &&
                   player.expression != CHARACTER_EXPRESSION_DMGRECVD_MAJOR ||
                   player.uExpressionTimePassed >= player.uExpressionTimeLength) {
            player.uExpressionTimeLength = 0;
            player.uExpressionTimePassed = 0;
            player.expression = expressionForCondition(condition);
        }
    }

    for (int i = 0; i < 2; ++i) {
        NPCData *hireling = &pParty->pHirelings[i];
        if (!hireling->dialogue_3_evt_id) continue;

        hireling->dialogue_2_evt_id += pMiscTimer->uTimeElapsed;
        if (hireling->dialogue_2_evt_id >= hireling->dialogue_3_evt_id) {
            hireling->dialogue_1_evt_id = 0;
            hireling->dialogue_2_evt_id = 0;
            hireling->dialogue_3_evt_id = 0;

            *hireling = NPCData();

            pParty->hirelingScrollPosition = 0;
            pParty->CountHirelings();
        }
    }
}

void Party::restAndHeal() {
    Character *pPlayer;         // esi@4
    bool have_vessels_soul;  // [sp+10h] [bp-8h]@10

    for (SpellBuff &buff : pParty->pPartyBuffs) {
        buff.Reset();
    }

    for (int pPlayerID = 0; pPlayerID < this->pCharacters.size(); ++pPlayerID) {
        pPlayer = &pParty->pCharacters[pPlayerID];
        for (SpellBuff &buff : pPlayer->pCharacterBuffs)
            buff.Reset();

        pPlayer->resetTempBonuses();
        if (pPlayer->conditions.HasAny({CONDITION_DEAD, CONDITION_PETRIFIED, CONDITION_ERADICATED})) {
            continue;
        }

        pPlayer->conditions.Reset(CONDITION_UNCONSCIOUS);
        pPlayer->conditions.Reset(CONDITION_DRUNK);
        pPlayer->conditions.Reset(CONDITION_FEAR);
        pPlayer->conditions.Reset(CONDITION_SLEEP);
        pPlayer->conditions.Reset(CONDITION_WEAK);

        pPlayer->timeToRecovery = 0;
        pPlayer->health = pPlayer->GetMaxHealth();
        pPlayer->mana = pPlayer->GetMaxMana();
        if (pPlayer->classType == CHARACTER_CLASS_LICH) {
            have_vessels_soul = false;
            for (uint i = 0; i < Character::INVENTORY_SLOT_COUNT; i++) {
                if (pPlayer->pInventoryItemList[i].uItemID == ITEM_QUEST_LICH_JAR_FULL && pPlayer->pInventoryItemList[i].uHolderPlayer == pPlayerID)
                    have_vessels_soul = true;
            }
            if (!have_vessels_soul) {
                pPlayer->health = pPlayer->GetMaxHealth() / 2;
                pPlayer->mana = pPlayer->GetMaxMana() / 2;
            }
        }

        if (pPlayer->conditions.Has(CONDITION_ZOMBIE)) {
            pPlayer->mana = 0;
            pPlayer->health /= 2;
        } else if (pPlayer->conditions.HasAny({CONDITION_POISON_SEVERE, CONDITION_DISEASE_SEVERE})) {
            pPlayer->health /= 4;
            pPlayer->mana /= 4;
        } else if (pPlayer->conditions.HasAny({CONDITION_POISON_MEDIUM, CONDITION_DISEASE_MEDIUM})) {
            pPlayer->health /= 3;
            pPlayer->mana /= 3;
        } else if (pPlayer->conditions.HasAny({CONDITION_POISON_WEAK, CONDITION_DISEASE_WEAK})) {
            pPlayer->health /= 2;
            pPlayer->mana /= 2;
        }
        if (pPlayer->conditions.Has(CONDITION_INSANE))
            pPlayer->mana = 0;
        updateCharactersAndHirelingsEmotions();
    }
    pParty->days_played_without_rest = 0;
}

void Rest(GameTime restTime) {
    if (restTime > GameTime::FromHours(4)) {
        Actor::InitializeActors();
    }

    pParty->GetPlayingTime() += restTime;

    for (Character &player : pParty->pCharacters) {
        player.Recover(restTime);  // ??
    }

    _494035_timed_effects__water_walking_damage__etc();
}

void restAndHeal(GameTime restTime) {
    pParty->GetPlayingTime() += restTime;

    pParty->pHirelings[0].bHasUsedTheAbility = false;
    pParty->pHirelings[1].bHasUsedTheAbility = false;

    pParty->uCurrentTimeSecond = pParty->GetPlayingTime().GetSecondsFraction();
    pParty->uCurrentMinute = pParty->GetPlayingTime().GetMinutesFraction();
    pParty->uCurrentHour = pParty->GetPlayingTime().GetHoursOfDay();
    pParty->uCurrentMonthWeek = pParty->GetPlayingTime().GetWeeksOfMonth();
    pParty->uCurrentDayOfMonth = pParty->GetPlayingTime().GetDaysOfMonth();
    pParty->uCurrentMonth = pParty->GetPlayingTime().GetMonthsOfYear();
    pParty->uCurrentYear = pParty->GetPlayingTime().GetYears() + game_starting_year;
    pParty->restAndHeal();

    for (Character &player : pParty->pCharacters) {
        player.timeToRecovery = 0;
        player.uNumDivineInterventionCastsThisDay = 0;
        player.uNumArmageddonCasts = 0;
        player.uNumFireSpikeCasts = 0;
    }

    pParty->updateCharactersAndHirelingsEmotions();
}
void Party::restOneFrame() {
    // Before each frame party rested for 6 minutes but that caused
    // resting to be too fast on high FPS
    // Now resting speed is roughly 6 game hours per second
    GameTime restTick = GameTime::FromMinutes(3 * pEventTimer->uTimeElapsed);

    if (remainingRestTime < restTick) {
        restTick = remainingRestTime;
    }

    if (restTick.Valid()) {
        Rest(restTick);
        remainingRestTime -= restTick;
        OutdoorLocation::LoadActualSkyFrame();
    }

    if (!remainingRestTime.Valid()) {
        if (currentRestType == REST_HEAL) {
            // Close rest screen when healing is done.
            // Resting type is reset on Escape processing
            engine->_messageQueue->addMessageCurrentFrame(UIMSG_Escape, 0, 0);
        } else if (currentRestType == REST_WAIT) {
            // Reset resting type after waiting is done.
            currentRestType = REST_NONE;
        }
    }
}

bool TestPartyQuestBit(PARTY_QUEST_BITS bit) {
    return pParty->_questBits[bit];
}

//----- (0047752B) --------------------------------------------------------
int Party::GetPartyReputation() {
    LocationInfo *ddm_dlv = &currentLocationInfo();

    int npcRep = 0;
    if (CheckHiredNPCSpeciality(Pirate)) npcRep += 5;
    if (CheckHiredNPCSpeciality(Burglar)) npcRep += 5;
    if (CheckHiredNPCSpeciality(Gypsy)) npcRep += 5;
    if (CheckHiredNPCSpeciality(Duper)) npcRep += 5;
    if (CheckHiredNPCSpeciality(FallenWizard)) npcRep += 5;
    return npcRep + ddm_dlv->reputation;
}

//----- (004269A2) --------------------------------------------------------
void Party::GivePartyExp(unsigned int pEXPNum) {
    signed int pActivePlayerCount;  // ecx@1
    int pLearningPercent;           // eax@13
    int playermodexp;

    if (pEXPNum > 0) {
        pActivePlayerCount = 0;
        for (Character &player : this->pCharacters) {
            if (player.conditions.HasNone({CONDITION_UNCONSCIOUS, CONDITION_DEAD, CONDITION_PETRIFIED, CONDITION_ERADICATED})) {
                pActivePlayerCount++;
            }
        }
        if (pActivePlayerCount) {
            pEXPNum = pEXPNum / pActivePlayerCount;
            for (Character &player : this->pCharacters) {
                if (player.conditions.HasNone({CONDITION_UNCONSCIOUS, CONDITION_DEAD, CONDITION_PETRIFIED, CONDITION_ERADICATED})) {
                    pLearningPercent = player.getLearningPercent();
                    playermodexp = pEXPNum + pEXPNum * pLearningPercent / 100;
                    player.experience += playermodexp;
                    if (player.experience > 4000000000) {
                        player.experience = 0;
                    }
                }
            }
        }
    }
}

void Party::partyFindsGold(int amount, GoldReceivePolicy policy) {
    int hirelingSalaries = 0;
    unsigned int goldToGain = amount;

    std::string status;
    if (policy == GOLD_RECEIVE_NOSHARE_SILENT) {
    } else if (policy == GOLD_RECEIVE_NOSHARE_MSG) {
        status = localization->FormatString(LSTR_FMT_YOU_FOUND_D_GOLD, amount);
    } else {
        FlatHirelings buf;
        buf.Prepare();

        for (int i = 0; i < buf.Size(); i++) {
            NPCProf prof = buf.Get(i)->profession;
            if (prof != NoProfession) {
                hirelingSalaries += pNPCStats->pProfessions[prof].uHirePrice;
            }
        }
        if (CheckHiredNPCSpeciality(Factor)) {
            goldToGain += (signed int)(10 * goldToGain) / 100;
        }
        if (CheckHiredNPCSpeciality(Banker)) {
            goldToGain += (signed int)(20 * goldToGain) / 100;
        }
        if (CheckHiredNPCSpeciality(Pirate)) {
            goldToGain += (signed int)(10 * goldToGain) / 100;
        }
        if (hirelingSalaries) {
            hirelingSalaries = (signed int)(goldToGain * hirelingSalaries / 100) / 100;
            if (hirelingSalaries < 1) {
                hirelingSalaries = 1;
            }
            status = localization->FormatString(LSTR_FMT_YOU_FOUND_D_GOLD_FOLLOWERS, goldToGain, hirelingSalaries);
        } else {
            status = localization->FormatString(LSTR_FMT_YOU_FOUND_D_GOLD, amount);
        }
    }
    AddGold(goldToGain - hirelingSalaries);
    if (status.length() > 0) {
        GameUI_SetStatusBar(status);
    }
}

void Party::dropHeldItem() {
    if (pPickedItem.uItemID == ITEM_NULL) {
        return;
    }

    SpriteObject sprite;
    sprite.uType = (SPRITE_OBJECT_TYPE)pItemTable->pItems[pPickedItem.uItemID].uSpriteID;
    sprite.uObjectDescID = pObjectList->ObjectIDByItemID(sprite.uType);
    sprite.spell_caster_pid = PID(OBJECT_Character, 0);
    sprite.vPosition = pos + Vec3i(0, 0, eyeLevel);
    sprite.uSoundID = 0;
    sprite.uFacing = 0;
    sprite.uAttributes = SPRITE_DROPPED_BY_PLAYER;
    sprite.uSectorID = pBLVRenderParams->uPartyEyeSectorID;
    sprite.uSpriteFrameID = 0;
    sprite.containing_item = pPickedItem;

    // extern int UnprojectX(int);
    // v9 = UnprojectX(v1->x);
    sprite.Create(_viewYaw, 184, 200, 0);  //+ UnprojectX(v1->x), 184, 200, 0);

    mouse->RemoveHoldingItem();
}

void Party::placeHeldItemInInventoryOrDrop() {
    // no picked item
    if (pPickedItem.uItemID == ITEM_NULL) {
        return;
    }

    if (!addItemToParty(&pPickedItem, true)) {
        dropHeldItem();
    }
}

bool Party::addItemToParty(ItemGen *pItem, bool isSilent) {
    if (!pItemTable->pItems[pItem->uItemID].uItemID_Rep_St) {
        pItem->SetIdentified();
    }

    if (!pItemTable->pItems[pItem->uItemID].iconName.empty()) {
        auto texture = assets->getImage_ColorKey(pItemTable->pItems[pItem->uItemID].iconName);
        int playerId = hasActiveCharacter() ? (pParty->_activeCharacter - 1) : 0;
        for (int i = 0; i < pCharacters.size(); i++, playerId++) {
            if (playerId >= pCharacters.size()) {
                playerId = 0;
            }
            int itemIndex = pCharacters[playerId].AddItem(-1, pItem->uItemID);
            if (itemIndex) {
                pCharacters[playerId].pInventoryItemList[itemIndex - 1] = *pItem;
                pItem->Reset();
                if (!isSilent) {
                    pAudioPlayer->playUISound(SOUND_gold01);
                    pCharacters[playerId].playReaction(SPEECH_FOUND_ITEM);
                }

                if (texture) {
                    texture->Release();
                }
                return true;
            }
        }
        if (texture) {
            texture->Release();
        }
    } else {
        logger->warning("Invalid picture_name detected ::addItem()");
    }
    return false;
}

int Party::getRandomActiveCharacterId(RandomEngine *rng) const {
    std::vector<int> activeCharacters = {};
    for (int i = 0; i < pCharacters.size(); i++) {
        if (pCharacters[i].CanAct()) {
            activeCharacters.push_back(i);
        }
    }
    if (!activeCharacters.empty()) {
        return activeCharacters[rng->random(activeCharacters.size())];
    }
    return -1;
}

bool Party::isPartyEvil() { return _questBits[QBIT_DARK_PATH]; }

bool Party::isPartyGood() { return _questBits[QBIT_LIGHT_PATH]; }

size_t Party::immolationAffectedActors(int *affected, size_t affectedArrSize, size_t effectRange) {
    int x, y, z;
    int affectedCount = 0;

    for (size_t i = 0; i < pActors.size(); ++i) {
        x = std::abs(pActors[i].pos.x - this->pos.x);
        y = std::abs(pActors[i].pos.y - this->pos.y);
        z = std::abs(pActors[i].pos.z - this->pos.z);
        if (int_get_vector_length(x, y, z) <= effectRange) {
            if (pActors[i].aiState != Dead && pActors[i].aiState != Dying &&
                pActors[i].aiState != Removed &&
                pActors[i].aiState != Disabled &&
                pActors[i].aiState != Summoned) {
                affected[affectedCount] = i;

                affectedCount++;
                if (affectedCount >= affectedArrSize)
                    break;
            }
        }
    }

    return affectedCount;
}

int getTravelTime() {
    signed int new_travel_time;  // esi@1

    new_travel_time = uDefaultTravelTime_ByFoot;
    if (CheckHiredNPCSpeciality(Guide)) --new_travel_time;
    if (CheckHiredNPCSpeciality(Tracker)) new_travel_time -= 2;
    if (CheckHiredNPCSpeciality(Pathfinder)) new_travel_time -= 3;
    if (CheckHiredNPCSpeciality(Explorer)) --new_travel_time;
    if (new_travel_time < 1) new_travel_time = 1;
    return new_travel_time;
}
// 6BD07C: using guessed type int uDefaultTravelTime_ByFoot;

//----- (004760D5) --------------------------------------------------------
PartyAction ActionQueue::Next() {
    if (!uNumActions) return PARTY_INVALID;

    PartyAction result = pActions[0];
    for (unsigned int i = 0; i < uNumActions - 1; ++i)
        pActions[i] = pActions[i + 1];
    --uNumActions;

    return result;
}

void Party::giveFallDamage(int distance) {
    for (Character &player : pParty->pCharacters) {  // receive falling damage
        if (!player.HasEnchantedItemEquipped(ITEM_ENCHANTMENT_OF_FEATHER_FALLING) &&
            !player.WearsItem(ITEM_ARTIFACT_HERMES_SANDALS, ITEM_SLOT_BOOTS)) {
            player.receiveDamage((int)((distance) * (uint64_t)(player.GetMaxHealth() / 10)) / 256, DMGT_PHISYCAL);
            int bonus = 20 - player.GetParameterBonus(player.GetActualEndurance());
            player.SetRecoveryTime(bonus * debug_non_combat_recovery_mul * flt_debugrecmod3);
        }
    }
}
