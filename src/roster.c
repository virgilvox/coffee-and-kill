/* roster.c: the fighters (see roster.h). MOHEEB and LUIS are the web version's pair; the
 * rest walk in from KEYHOLDER: its three keyholders and the people they fought that day. */
#include "roster.h"
#include <string.h>

#define PAGE(glb_, rig_, sc, gx, gy, gz) .kind = BODY_PAGE, .glb = glb_, .rig = rig_, .scale = sc, .grip = {gx, gy, gz}
#define KH(letter, sc) .kind = BODY_KH, .body = letter, .scale = sc

const roster_entry ROSTER[] = {
  { "moheeb", "MOHEEB", "THE BARISTA", "Pulls a shot, pulls a wrench. Same wrist.", 0xf2ab1e, "pipe", NULL, true,
    PAGE("char_b.glb", "rig_b.bin", 0.97f, 0.986f, 0.11f, 0.129f) },
  { "luis", "LUIS", "THE FABRICATOR", "Measures twice. Swings once. Hard.", 0x5e93c4, "sledge", NULL, true,
    PAGE("char_a.glb", "rig_a.bin", 1.04f, 0.979f, 0.204f, 0) },
  { "vee", "VEE", "KEYHOLDER", "Has the keys today. Opens at nine, closes at midnight.", 0xf2ab1e, "arc", "Yes", false,
    KH("C", 1.0f) },
  { "dex", "DEX", "KEYHOLDER", "Knows where every tool lives and who borrowed it.", 0x6cb4ee, "staff", "Yes", false,
    KH("D", 1.0f) },
  { "ruth", "RUTH", "KEYHOLDER", "Runs orientation. Handled carefully, by trained people.", 0xf07a52, "spear", "Yes", false,
    KH("B", 1.0f) },
  { "vic", "VINYL VIC", "YEARS OF EXPERIENCE", "Brought a sheet of PVC to the laser. Calls everyone chief.", 0xe05a9c, "staff", "Dance_Loop", false,
    KH("E2", 1.0f) },
  { "rennick", "RENNICK", "SHARED BIKE", "Relax. It's a shared bike. He's sharing.", 0xa8d060, "twin", "Yes", false,
    KH("H", 1.0f) },
  { "dobbs", "DOBBS", "THE MUSCLE", "Hammer was a strong word. He brought one anyway.", 0xb09060, "maul", "Celebration", false,
    KH("B2", 1.08f) },
  { "champ", "HIGH SCORE", "ARCADE CHAMPION", "Has been waiting all day to say ready, fight.", 0x60e0e0, "twin", "Dance_Loop", false,
    KH("Y", 1.0f) },
  { "bex", "BEX", "THE FRIDGE", "Labeled, dated, or gone.", 0x90d8f0, "spear", "Yes", false,
    KH("Z", 1.0f) },
  { "cal", "COPPER CAL", "SCRAP DEALER", "Two bucks a pound, partner. It's just math.", 0xd07030, "maul", "Celebration", false,
    KH("C2", 1.1f) },
  { "root", "ROOT", "THE OLD BUILDING", "Carried the bench over himself. Will carry you too.", 0x88a0a8, "maul", "Celebration", false,
    KH("O", 1.04f) },
  { "brant", "BRANT HOLLOWAY", "GROWTH", "Wants to take this offline. Brought a team.", 0xe8c860, "arc", "Yes", false,
    KH("G2", 1.02f) },
};
const int ROSTER_N = (int)(sizeof ROSTER / sizeof ROSTER[0]);

int roster_index(const char *key) {
  for (int i = 0; key && i < ROSTER_N; i++) if (!strcmp(ROSTER[i].key, key)) return i;
  return -1;
}
const roster_entry *roster_get(int i) { return &ROSTER[i < 0 ? 0 : i >= ROSTER_N ? ROSTER_N - 1 : i]; }
int roster_web_count(void) { int n = 0; for (int i = 0; i < ROSTER_N; i++) n += ROSTER[i].web; return n; }
