// JoJo's Bizarre Adventure: All-Star Battle R's shaders out of the game image: what the SPIR-V translator
// (spirv_harness --batch) needs, in the layout games/p5r/import/p5r_shaders.h writes:
//   <out>/<group>/manifest.tsv, code_<sha1>.bin, ctrl_<sha1>.bin
// The game ships its compiled NVN shaders as plain files in the RomFS: data/out/*.nvn (one "BNVN" blob each) and the
// NUP4 packs data/system/*.nsh (a row of "BNVN" blobs). Every blob starts with a 0x2E0-byte header, then the NVN
// control section (docs/nvn/SHADERS.md). The header's word 53 (blob+0xD4) is the code block's offset from the end of
// the header, word 54 its size (= control +0x704): verified on 384 graphics blobs of the 2.3.3 dump, and on the 25
// compute blobs, whose code has no 0x30/0x100-byte NVN header (control +0x708 = 0).
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "nxgame.h"

namespace jojo {

struct Report {
    long blobs = 0;       // BNVN blobs with a control section
    long written = 0;     // distinct code blocks written
    long duplicates = 0;  // blocks an earlier blob already wrote
    long invalid = 0;     // blobs that failed a layout check (not written)
    std::vector<std::string> notes;  // the first failures, for the log
};

// One buffer (a .nvn file, a .nsh pack, or main's flat image with exefs = true): the blocks found in it written to
// <out_dir>/<group>/ . Returns the blocks written (distinct across calls sharing `seen`). Throws nothing it cannot
// describe: failures are counted in `rep`.
long extract_buffer(const std::string& group, const std::vector<uint8_t>& data, bool exefs, const std::string& out_dir,
                    std::set<std::string>& seen, Report& rep);

// The whole game: every *.nvn and *.nsh of the RomFS, then the blocks built into main. Distinct blocks written, or -1
// with `error` set.
long extract_shaders(const nxi::GameImage& game, const std::string& out_dir,
                     const std::function<void(int, int)>& progress, Report& rep, std::string& error);

}  // namespace jojo
