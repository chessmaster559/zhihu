#pragma once
#include "pet_controller.h"

struct PetAnimationSpec {
    const char* asset;
    bool single;
    bool portrait;
    int rotation;
};

inline PetAnimationSpec GetPetAnimationSpec(PetAction action) {
    switch (action) {
        case PetAction::Setup:
            return {nullptr, false, true, 0};
        case PetAction::Thinking:
            return {"pet_thinking", true, false, 0};
        case PetAction::Surprise:
        case PetAction::Celebrating:
        case PetAction::Shaking:
            return {"pet_greeting", true, false, 0};
        case PetAction::Sway:
            return {"pet_sway", true, false, 0};
        case PetAction::Playing:
            return {"pet_play", true, false, 0};
        case PetAction::Sleeping:
            return {"pet_sleep", false, false, 0};
        case PetAction::TiltLeft:
            return {"pet_idle", true, false, -150};
        case PetAction::TiltRight:
            return {"pet_idle", true, false, 150};
        case PetAction::UpsideDown:
            return {"pet_idle", true, false, 1800};
        default:
            return {"pet_idle", false, false, 0};
    }
}
