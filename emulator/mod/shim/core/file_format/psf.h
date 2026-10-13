// BBSC mod: the game's param.sfo is not read; the DLL only runs on an image whose patch sites all
// match CUSA03173 01.09, so that is what it reports.
#pragma once
#include <optional>
#include <string_view>

class PSF {
public:
    std::optional<std::string_view> GetString(std::string_view key) const {
        if (key == "APP_VER") {
            return "01.09";
        }
        if (key == "TITLE_ID") {
            return "CUSA03173";
        }
        return std::nullopt;
    }
};
