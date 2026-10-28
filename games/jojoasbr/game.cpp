// JoJo's Bizarre Adventure: All-Star Battle R (010017A0128C4000), version 2.3.3 = base 1.0.0 + update (main build ID
// 3D4E3A9252AA6C63). CyberConnect2's in-house engine on NVN; the game runs natively (NX_NCE) from the player's own
// dump, so this file holds only what the shared runtime needs to know: no offsets, no hooks. If a game specific
// problem appears, patches go in after_init() below (a hook needs a symbol or an offset found on the real game).
#include <cstdlib>

#include <nx/game.h>
#include <nx/runtime.h>

namespace nx {

// The title is also the Android data folder (Internal storage/JoJo ASBR): no apostrophe or colon in it.
const game::Info game::kInfo = {"jojoasbr", "JoJo ASBR", 0x010017a0128c4000ull, "io.github.jojorecomp",
                                "private/jojoasbr", 0x2000000 /* main.npdm: system resource 32 MiB */,
                                "3D4E3A9252AA6C63" /* 2.3.3 */};

void game::after_init() {
#if defined(_WIN32)
    if (!std::getenv("NX_GAME_LANGUAGES")) _putenv_s("NX_GAME_LANGUAGES", "ja en-us fr de it es");
#else
    // The text languages of the game (a device in another one gets English, not Japanese).
    setenv("NX_GAME_LANGUAGES", "ja en-us fr de it es", 0);
#endif
}
void game::renderer_start() {}
void game::first_frame() {}

}  // namespace nx
