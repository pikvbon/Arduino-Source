/*  Pokeball Sprite Reader
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#ifndef PokemonAutomation_PokemonBDSP_PokeballSpriteReader_H
#define PokemonAutomation_PokemonBDSP_PokeballSpriteReader_H

#include "CommonFramework/ImageMatch/CroppedImageDictionaryMatcher.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonBDSP{


class PokeballSpriteMatcher : public ImageMatch::CroppedImageDictionaryMatcher{
public:
    PokeballSpriteMatcher(double min_euclidean_distance = 100);

private:
    virtual ImageRGB32 process_image(const ImageViewRGB32& image, QRgb& background) const override;

private:
    double m_min_euclidean_distance_squared;
};



}
}
}
#endif
