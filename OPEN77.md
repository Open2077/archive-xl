# ArchiveXL 1.28.0 + Open77 (Cyberpunk 2.5)

**Unofficial.** Branch `open77/1.28` of [Open2077/archive-xl](https://github.com/Open2077/archive-xl): the official
[1.28.0](https://github.com/psiberx/cp2077-archive-xl/releases/tag/v1.28.0) (the first psiberx release for 2.5),
plus the world-streaming work of our own 2.5 port (`open77/cyberpunk-2.5`, 1.27.5-open77.x) that 1.28.0 does not have.
Everything else (addresses, archive scopes, customization, the attachment-slot deadlock) is psiberx's 1.28.0.
Releases are tagged `v1.28.0-open77.N`; the package is game-root relative, like the official one.

## What it adds to 1.28.0 (`src/App/Extensions/WorldStreaming/Extension.cpp`, `src/Red/StreamingSector.hpp`)

- **Deleting a cooked placement no longer crashes the game.** 1.28.0 swaps a deleted `worldCooked*` setup for a
  dummy `worldStaticMeshNode` and keeps the setup's instance index; the game then reads the dummy as a cooked body
  and faults reading 0xFFFFFFFFFFFFFFFF at `Cyberpunk2077.exe+0x20BE897`
  ([psiberx/cp2077-archive-xl#36](https://github.com/psiberx/cp2077-archive-xl/issues/36)). Here a cooked
  setup gets the dummy only with its instance index reset to -1 (2.5 indexes the placements of more classes than
  `worldCooked*`: instanced destructibles, physical destruction...), and a cooked static mesh is only scaled to 0.
- **Cooked collision actors** (`worldCookedCollisionNode`): `actorDeletions` and `actorMutations` edit the actors of
  one placement in the node's compiled data (2.31 layout kept per run), guarded by `expectedActors`. 1.28.0 does not
  know the class: a removed game mesh kept its collision.
- The uncooked `worldCollisionNode` of 2.5 has a new layout (0x120 -> 0xC0): its actors are not addressed with
  the 2.31 offsets any more.

Verified in game on 2026-10-26 (LightRP): five NCWE worlds that crashed the official 1.28.0 (NCPD Watson,
`entrepot1`, `appart3`, the Kabuki studio) load and stay up, sectors patched with no error; deleting every cooked
collision placement under a player's feet makes the player fall through the street (client log `floor=absent`).

## Building

As upstream: `xmake f -m release -y` then `xmake build -y` (vendor/RED4ext.SDK as pinned by 1.28.0).
