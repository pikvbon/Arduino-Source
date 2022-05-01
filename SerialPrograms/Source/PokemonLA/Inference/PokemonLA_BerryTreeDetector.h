/*  Berry Tree Detector
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#ifndef PokemonAutomation_PokemonLA_BerryTreeDetector_H
#define PokemonAutomation_PokemonLA_BerryTreeDetector_H

#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/InferenceInfra/VisualInferenceCallback.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLA{


class BerryTreeDetector : public VisualInferenceCallback{
public:
    BerryTreeDetector();

    virtual void make_overlays(VideoOverlaySet& items) const override;

    //  Return true if the inference session should stop.
    virtual bool process_frame(const QImage& frame, WallClock timestamp) override;

private:
};


}
}
}
#endif
