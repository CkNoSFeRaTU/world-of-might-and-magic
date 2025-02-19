#pragma once

#include "GUI/UI/UIHouses.h"
#include "GUI/UI/UIHouseEnums.h"

class GUIWindow_Jail : public GUIWindow_House {
 public:
    explicit GUIWindow_Jail(HOUSE_ID houseId) : GUIWindow_House(houseId) {}
    virtual ~GUIWindow_Jail() {}

    virtual void houseDialogueOptionSelected(DIALOGUE_TYPE option) override;
    virtual void houseSpecificDialogue() override;
};
